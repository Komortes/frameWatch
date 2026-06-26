#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "framewatch/core/frame_sample.h"

namespace framewatch {

// Receives FrameSample data from a client (e.g. injected DLL in the target process).
//
// Endpoint:
//   Windows : \\.\pipe\framewatch-live
//   Unix    : /tmp/framewatch-live.sock
//
// Protocol: newline-delimited JSON, one sample per line:
//   {"frame":1,"ts":0.016,"ft_ms":16.666,"fps":60.0}
//
// Usage:
//   IpcServer srv;
//   if (srv.Start()) { /* running */ }
//   // in frame loop:
//   for (auto& s : srv.DrainSamples()) { session.PushSample(s); }
//   srv.Stop();
class IpcServer {
public:
#ifdef _WIN32
    static constexpr std::string_view kEndpoint{R"(\\.\pipe\framewatch-live)"};
#else
    static constexpr std::string_view kEndpoint{"/tmp/framewatch-live.sock"};
#endif

    IpcServer();
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    // Start the background accept/read thread. Returns false if already running
    // or if the endpoint cannot be created.
    bool Start();

    // Signal the server to stop and block until the thread exits.
    void Stop() noexcept;

    bool IsRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

    // True when a client is actively connected and sending data.
    bool HasClient() const noexcept { return has_client_.load(std::memory_order_relaxed); }

    // Move all queued samples to the caller. Non-blocking; safe to call from the main loop.
    std::vector<FrameSample> DrainSamples();

private:
    void ServerThread();
    void EnqueueLine(const std::string& line);

    std::atomic<bool> running_{false};
    std::atomic<bool> has_client_{false};

    std::mutex queue_mutex_;
    std::deque<FrameSample> sample_queue_;

    std::thread thread_;

#ifdef _WIN32
    void* stop_event_{nullptr};  // HANDLE — avoids Windows.h in this header
#else
    int server_fd_{-1};
    int wake_fds_[2]{-1, -1};  // self-pipe: [0]=read [1]=write, used to interrupt accept()
#endif
};

}  // namespace framewatch
