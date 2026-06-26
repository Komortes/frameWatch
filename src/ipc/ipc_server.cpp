#include "framewatch/ipc/ipc_server.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

// ---------------------------------------------------------------------------
// Platform includes
// ---------------------------------------------------------------------------

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace framewatch {

// ---------------------------------------------------------------------------
// Common helpers
// ---------------------------------------------------------------------------

void IpcServer::EnqueueLine(const std::string& line) {
    if (line.empty()) return;
    try {
        const auto j = nlohmann::json::parse(line);
        FrameSample s;
        s.frame_index       = j.value("frame", std::uint64_t{0});
        s.timestamp_seconds = j.value("ts",    0.0);
        s.frametime_ms      = j.value("ft_ms", 0.0);
        s.fps               = j.value("fps",   0.0);
        if (s.frametime_ms > 0.0) {
            std::lock_guard lock(queue_mutex_);
            sample_queue_.push_back(s);
        }
    } catch (...) {
        // malformed line — silently skip
    }
}

std::vector<FrameSample> IpcServer::DrainSamples() {
    std::lock_guard lock(queue_mutex_);
    std::vector<FrameSample> out(sample_queue_.begin(), sample_queue_.end());
    sample_queue_.clear();
    return out;
}

// ---------------------------------------------------------------------------
// Windows implementation
// ---------------------------------------------------------------------------

#ifdef _WIN32

IpcServer::IpcServer() {
    stop_event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

IpcServer::~IpcServer() {
    Stop();
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
}

bool IpcServer::Start() {
    if (running_.exchange(true)) return false;
    ResetEvent(static_cast<HANDLE>(stop_event_));
    thread_ = std::thread(&IpcServer::ServerThread, this);
    return true;
}

void IpcServer::Stop() noexcept {
    if (!running_.exchange(false)) return;
    SetEvent(static_cast<HANDLE>(stop_event_));
    if (thread_.joinable()) thread_.join();
    has_client_.store(false);
}

void IpcServer::ServerThread() {
    HANDLE stop_ev = static_cast<HANDLE>(stop_event_);

    while (running_.load(std::memory_order_relaxed)) {
        HANDLE pipe = CreateNamedPipeA(
            kEndpoint.data(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 0, 4096, 0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) break;

        // Wait for client — use overlapped ConnectNamedPipe so we can cancel via stop_ev.
        OVERLAPPED ov{};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);

        HANDLE wait_handles[] = {ov.hEvent, stop_ev};
        DWORD w = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        CloseHandle(ov.hEvent);

        if (w != WAIT_OBJECT_0 || !running_.load(std::memory_order_relaxed)) {
            CancelIo(pipe);
            CloseHandle(pipe);
            break;
        }

        has_client_.store(true, std::memory_order_relaxed);

        // Read loop: poll + ReadFile until client disconnects or stop is requested.
        std::string line_buf;
        char chunk[256];

        while (running_.load(std::memory_order_relaxed)) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
            if (available == 0) {
                Sleep(1);
                continue;
            }
            DWORD n = std::min(available, static_cast<DWORD>(sizeof(chunk) - 1));
            DWORD read = 0;
            if (!ReadFile(pipe, chunk, n, &read, nullptr)) break;
            chunk[read] = '\0';
            line_buf.append(chunk, read);

            std::size_t pos;
            while ((pos = line_buf.find('\n')) != std::string::npos) {
                EnqueueLine(line_buf.substr(0, pos));
                line_buf.erase(0, pos + 1);
            }
        }

        has_client_.store(false, std::memory_order_relaxed);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

// ---------------------------------------------------------------------------
// Unix implementation
// ---------------------------------------------------------------------------

#else

IpcServer::IpcServer() {
    // Self-pipe pair used to interrupt accept() from Stop().
    wake_fds_[0] = wake_fds_[1] = -1;
}

IpcServer::~IpcServer() {
    Stop();
}

bool IpcServer::Start() {
    if (running_.exchange(true)) return false;

    // Self-pipe for waking accept().
    if (pipe(wake_fds_) != 0) {
        running_.store(false);
        return false;
    }
    fcntl(wake_fds_[0], F_SETFD, FD_CLOEXEC);
    fcntl(wake_fds_[1], F_SETFD, FD_CLOEXEC);

    // Create Unix domain socket.
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        close(wake_fds_[0]); close(wake_fds_[1]);
        wake_fds_[0] = wake_fds_[1] = -1;
        running_.store(false);
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // kEndpoint fits — it is "/tmp/framewatch-live.sock" (27 chars)
    const auto ep = std::string(kEndpoint);
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ep.c_str());

    ::unlink(addr.sun_path);  // remove stale socket from previous run

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(server_fd_, 1) != 0) {
        close(server_fd_); server_fd_ = -1;
        close(wake_fds_[0]); close(wake_fds_[1]);
        wake_fds_[0] = wake_fds_[1] = -1;
        running_.store(false);
        return false;
    }

    thread_ = std::thread(&IpcServer::ServerThread, this);
    return true;
}

void IpcServer::Stop() noexcept {
    if (!running_.exchange(false)) return;

    // Wake the accept() call by writing to the self-pipe.
    if (wake_fds_[1] >= 0) {
        const char byte = 0;
        // Best-effort one-byte wake; failure is harmless (thread checks running_).
        ssize_t wake_result = write(wake_fds_[1], &byte, 1);
        (void)wake_result;
    }

    if (thread_.joinable()) thread_.join();

    if (server_fd_ >= 0) {
        const auto ep = std::string(kEndpoint);
        ::unlink(ep.c_str());
        close(server_fd_);
        server_fd_ = -1;
    }
    if (wake_fds_[0] >= 0) { close(wake_fds_[0]); wake_fds_[0] = -1; }
    if (wake_fds_[1] >= 0) { close(wake_fds_[1]); wake_fds_[1] = -1; }

    has_client_.store(false);
}

void IpcServer::ServerThread() {
    while (running_.load(std::memory_order_relaxed)) {
        // Use select() to wait on either accept() or the wake-pipe.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd_, &rfds);
        FD_SET(wake_fds_[0], &rfds);
        int nfds = std::max(server_fd_, wake_fds_[0]) + 1;

        if (select(nfds, &rfds, nullptr, nullptr, nullptr) <= 0) break;
        if (FD_ISSET(wake_fds_[0], &rfds)) break;
        if (!FD_ISSET(server_fd_, &rfds)) continue;

        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) break;

        has_client_.store(true, std::memory_order_relaxed);

        // Read loop.
        std::string line_buf;
        char chunk[256];

        while (running_.load(std::memory_order_relaxed)) {
            // select with short timeout so we can check running_ regularly.
            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(client_fd, &rset);
            FD_SET(wake_fds_[0], &rset);
            timeval tv{0, 5000};  // 5 ms
            int sel = select(std::max(client_fd, wake_fds_[0]) + 1, &rset, nullptr, nullptr, &tv);
            if (sel < 0) break;
            if (sel == 0) continue;
            if (FD_ISSET(wake_fds_[0], &rset)) break;

            ssize_t n = recv(client_fd, chunk, sizeof(chunk) - 1, 0);
            if (n <= 0) break;
            chunk[n] = '\0';
            line_buf.append(chunk, static_cast<std::size_t>(n));

            std::size_t pos;
            while ((pos = line_buf.find('\n')) != std::string::npos) {
                EnqueueLine(line_buf.substr(0, pos));
                line_buf.erase(0, pos + 1);
            }
        }

        has_client_.store(false, std::memory_order_relaxed);
        close(client_fd);
    }
}

#endif

}  // namespace framewatch
