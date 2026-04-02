#include "framewatch/bootstrap/injected_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace {

struct InjectOptions {
    bool list_windows{false};
    bool eject{false};
    DWORD pid{0};
    std::wstring window_title;
    std::filesystem::path dll_path;
    int wait_ms{5000};
};

struct WindowCandidate {
    HWND handle{nullptr};
    DWORD pid{0};
    std::wstring title;
};

bool ParsePositiveInteger(const wchar_t* value, int& output) {
    if (value == nullptr || *value == L'\0') {
        return false;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParsePid(const wchar_t* value, DWORD& output) {
    int parsed = 0;
    if (!ParsePositiveInteger(value, parsed)) {
        return false;
    }
    output = static_cast<DWORD>(parsed);
    return true;
}

std::filesystem::path CurrentExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2U);
    }
}

std::filesystem::path ResolveDefaultDllPath() {
    const std::filesystem::path executable_path = CurrentExecutablePath();
    if (executable_path.empty()) {
        return "framewatch_dx11_overlay.dll";
    }
    return executable_path.parent_path() / "framewatch_dx11_overlay.dll";
}

std::filesystem::path ResolveStatusPath(const std::filesystem::path& dll_path) {
    return dll_path.parent_path() / std::string(framewatch::kInjectedStatusDirectory) /
           std::string(framewatch::kInjectedStatusFileName);
}

// Returns 32 or 64 for x86/x64 DLLs. Returns 0 on parse error.
int GetDllBitness(const std::filesystem::path& dll_path) {
    std::ifstream file(dll_path, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }

    IMAGE_DOS_HEADER dos_header{};
    file.read(reinterpret_cast<char*>(&dos_header), sizeof(dos_header));
    if (!file || dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    if (dos_header.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))) {
        return 0;
    }
    file.seekg(dos_header.e_lfanew);
    if (!file) {
        return 0;
    }
    DWORD signature = 0;
    file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    if (!file || signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    IMAGE_FILE_HEADER file_header{};
    file.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!file) {
        return 0;
    }

    switch (file_header.Machine) {
        case IMAGE_FILE_MACHINE_I386:
            return 32;
        case IMAGE_FILE_MACHINE_AMD64:
            return 64;
        default:
            return 0;
    }
}

// Returns 32 or 64. Returns 0 if the bitness cannot be determined.
int GetProcessBitness(HANDLE process_handle) {
    BOOL target_wow64 = FALSE;
    if (!IsWow64Process(process_handle, &target_wow64)) {
        return 0;
    }

    if (target_wow64) {
        // 32-bit process running under WOW64 on a 64-bit OS
        return 32;
    }

    // Not WOW64: either native 64-bit on 64-bit OS, or 32-bit on 32-bit OS.
    // Check our own WOW64 status to disambiguate.
    BOOL self_wow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &self_wow64)) {
        return 0;
    }

    if (self_wow64) {
        // Injector is 32-bit on 64-bit OS, target is not WOW64 → target is native 64-bit
        return 64;
    }

    // Both native processes: same bitness as the injector
    return (sizeof(void*) == 8) ? 64 : 32;
}

void PrintUsage() {
    std::wcout
        << L"FrameWatch injector\n"
        << L"Usage:\n"
        << L"  framewatch_injector --list-windows [--dll <path>]\n"
        << L"  framewatch_injector --pid <pid> [--dll <path>] [--wait-ms <ms>]\n"
        << L"  framewatch_injector --window-title <substring> [--dll <path>] [--wait-ms <ms>]\n"
        << L"  framewatch_injector --eject --pid <pid> [--dll <path>] [--wait-ms <ms>]\n"
        << L"  framewatch_injector --eject --window-title <substring> [--dll <path>] [--wait-ms <ms>]\n";
}

std::wstring ToLowerCopy(std::wstring_view input) {
    std::wstring output(input);
    std::transform(output.begin(), output.end(), output.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return output;
}

BOOL CALLBACK CollectWindowsProc(HWND handle, LPARAM lparam) {
    if (IsWindowVisible(handle) == 0) {
        return TRUE;
    }

    const int title_length = GetWindowTextLengthW(handle);
    if (title_length <= 0) {
        return TRUE;
    }

    std::wstring title(static_cast<std::size_t>(title_length + 1), L'\0');
    const int copied = GetWindowTextW(handle, title.data(), title_length + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<std::size_t>(copied));

    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    if (pid == 0) {
        return TRUE;
    }

    auto* windows = reinterpret_cast<std::vector<WindowCandidate>*>(lparam);
    windows->push_back(WindowCandidate{handle, pid, std::move(title)});
    return TRUE;
}

std::vector<WindowCandidate> EnumerateWindows() {
    std::vector<WindowCandidate> windows;
    EnumWindows(&CollectWindowsProc, reinterpret_cast<LPARAM>(&windows));
    std::sort(windows.begin(), windows.end(), [](const WindowCandidate& left, const WindowCandidate& right) {
        if (left.pid != right.pid) {
            return left.pid < right.pid;
        }
        return left.title < right.title;
    });
    return windows;
}

void PrintWindows(const std::vector<WindowCandidate>& windows) {
    if (windows.empty()) {
        std::wcout << L"No visible titled windows were found.\n";
        return;
    }

    for (const WindowCandidate& window : windows) {
        std::wcout << L"pid=" << window.pid << L" hwnd=0x" << std::hex
                   << reinterpret_cast<std::uintptr_t>(window.handle) << std::dec
                   << L" title=\"" << window.title << L"\"\n";
    }
}

std::optional<DWORD> ResolvePidByWindowTitle(std::wstring_view query) {
    const std::wstring lowered_query = ToLowerCopy(query);
    std::vector<WindowCandidate> matches;

    for (const WindowCandidate& window : EnumerateWindows()) {
        if (ToLowerCopy(window.title).find(lowered_query) != std::wstring::npos) {
            matches.push_back(window);
        }
    }

    if (matches.empty()) {
        std::wcerr << L"No visible window matched \"" << query << L"\".\n";
        return std::nullopt;
    }

    if (matches.size() > 1U) {
        std::wcerr << L"Window title query matched multiple targets. Use --pid or a more specific title:\n";
        PrintWindows(matches);
        return std::nullopt;
    }

    return matches.front().pid;
}

InjectOptions ParseArgs(int argc, wchar_t** argv, bool& ok) {
    InjectOptions options;
    options.dll_path = ResolveDefaultDllPath();
    ok = true;

    for (int index = 1; index < argc; ++index) {
        const std::wstring_view arg = argv[index];
        if (arg == L"--list-windows") {
            options.list_windows = true;
        } else if (arg == L"--eject") {
            options.eject = true;
        } else if (arg == L"--pid" && (index + 1) < argc) {
            ok = ParsePid(argv[++index], options.pid);
        } else if (arg == L"--window-title" && (index + 1) < argc) {
            options.window_title = argv[++index];
        } else if (arg == L"--dll" && (index + 1) < argc) {
            options.dll_path = argv[++index];
        } else if (arg == L"--wait-ms" && (index + 1) < argc) {
            ok = ParsePositiveInteger(argv[++index], options.wait_ms);
        } else if (arg == L"--help" || arg == L"-h") {
            PrintUsage();
            std::exit(EXIT_SUCCESS);
        } else {
            ok = false;
        }

        if (!ok) {
            break;
        }
    }

    return options;
}

bool InjectLibrary(DWORD pid, const std::filesystem::path& dll_path) {
    const std::filesystem::path absolute_dll_path = std::filesystem::absolute(dll_path);
    const std::wstring dll_path_wide = absolute_dll_path.wstring();
    const std::size_t byte_count = (dll_path_wide.size() + 1U) * sizeof(wchar_t);

    HANDLE process_handle = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                        FALSE,
                                        pid);
    if (process_handle == nullptr) {
        std::wcerr << L"OpenProcess failed for pid " << pid << L".\n";
        return false;
    }

    const int dll_bits = GetDllBitness(absolute_dll_path);
    const int proc_bits = GetProcessBitness(process_handle);
    if (dll_bits == 0) {
        std::wcerr << L"Warning: could not determine DLL architecture, proceeding anyway.\n";
    } else if (proc_bits == 0) {
        std::wcerr << L"Warning: could not determine target process architecture, proceeding anyway.\n";
    } else if (dll_bits != proc_bits) {
        std::wcerr << L"Architecture mismatch: target process is " << proc_bits
                   << L"-bit but DLL is " << dll_bits << L"-bit.\n";
        CloseHandle(process_handle);
        return false;
    }

    LPVOID remote_path = VirtualAllocEx(
        process_handle, nullptr, byte_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        std::wcerr << L"VirtualAllocEx failed.\n";
        CloseHandle(process_handle);
        return false;
    }

    const BOOL wrote_path = WriteProcessMemory(
        process_handle, remote_path, dll_path_wide.c_str(), byte_count, nullptr);
    if (wrote_path == 0) {
        std::wcerr << L"WriteProcessMemory failed.\n";
        VirtualFreeEx(process_handle, remote_path, 0, MEM_RELEASE);
        CloseHandle(process_handle);
        return false;
    }

    HMODULE kernel32_module = GetModuleHandleW(L"kernel32.dll");
    if (kernel32_module == nullptr) {
        std::wcerr << L"GetModuleHandleW(kernel32.dll) failed.\n";
        VirtualFreeEx(process_handle, remote_path, 0, MEM_RELEASE);
        CloseHandle(process_handle);
        return false;
    }

    const FARPROC load_library = GetProcAddress(kernel32_module, "LoadLibraryW");
    if (load_library == nullptr) {
        std::wcerr << L"GetProcAddress(LoadLibraryW) failed.\n";
        VirtualFreeEx(process_handle, remote_path, 0, MEM_RELEASE);
        CloseHandle(process_handle);
        return false;
    }

    HANDLE thread_handle = CreateRemoteThread(process_handle,
                                              nullptr,
                                              0,
                                              reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library),
                                              remote_path,
                                              0,
                                              nullptr);
    if (thread_handle == nullptr) {
        std::wcerr << L"CreateRemoteThread failed.\n";
        VirtualFreeEx(process_handle, remote_path, 0, MEM_RELEASE);
        CloseHandle(process_handle);
        return false;
    }

    WaitForSingleObject(thread_handle, INFINITE);

    DWORD load_result = 0;
    GetExitCodeThread(thread_handle, &load_result);

    CloseHandle(thread_handle);
    VirtualFreeEx(process_handle, remote_path, 0, MEM_RELEASE);
    CloseHandle(process_handle);

    if (load_result == 0) {
        std::wcerr << L"Remote LoadLibraryW returned 0.\n";
        return false;
    }

    return true;
}

std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::optional<std::string> ParseStatusLine(std::string_view content, std::string_view key) {
    const std::string prefix = std::string(key) + "=";
    const std::size_t position = content.find(prefix);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t value_start = position + prefix.size();
    const std::size_t value_end = content.find('\n', value_start);
    return std::string(content.substr(value_start, value_end - value_start));
}

bool WaitForBootstrapStatus(const std::filesystem::path& status_path, int wait_ms) {
    if (wait_ms <= 0) {
        return true;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    std::string last_reported_content;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(status_path)) {
            const std::string content = ReadFileText(status_path);
            if (content == last_reported_content) {
                Sleep(100);
                continue;
            }
            last_reported_content = content;

            const auto state = ParseStatusLine(content, "state");
            const auto message = ParseStatusLine(content, "message");
            const auto hook = ParseStatusLine(content, "hook");
            const auto renderer = ParseStatusLine(content, "renderer");

            if (state.has_value()) {
                std::cout << "bootstrap.state=" << *state << '\n';
            }
            if (message.has_value()) {
                std::cout << "bootstrap.message=" << *message << '\n';
            }
            if (hook.has_value()) {
                std::cout << "bootstrap.hook=" << *hook << '\n';
            }
            if (renderer.has_value()) {
                std::cout << "bootstrap.renderer=" << *renderer << '\n';
            }

            if (state == std::optional<std::string>{"running"}) {
                return true;
            }
            if (state == std::optional<std::string>{"failed"}) {
                return false;
            }
        }

        Sleep(100);
    }

    std::cout << "bootstrap.state=timeout\n";
    return false;
}

bool WaitForEjectStatus(const std::filesystem::path& status_path, int wait_ms) {
    if (wait_ms <= 0) {
        return true;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    std::string last_reported_content;

    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(status_path)) {
            const std::string content = ReadFileText(status_path);
            if (content == last_reported_content) {
                Sleep(100);
                continue;
            }
            last_reported_content = content;

            const auto state = ParseStatusLine(content, "state");
            const auto message = ParseStatusLine(content, "message");

            if (state.has_value()) {
                std::cout << "bootstrap.state=" << *state << '\n';
            }
            if (message.has_value()) {
                std::cout << "bootstrap.message=" << *message << '\n';
            }

            if (state == std::optional<std::string>{"uninitialized"}) {
                return true;
            }
            if (state == std::optional<std::string>{"failed"}) {
                return false;
            }
        } else if (!last_reported_content.empty()) {
            // File was present before but now gone — DLL has fully unloaded
            return true;
        }

        Sleep(100);
    }

    std::cout << "bootstrap.state=timeout\n";
    return false;
}

bool EjectLibrary(DWORD pid, const std::filesystem::path& status_path, int wait_ms) {
    const std::wstring event_name =
        std::wstring(framewatch::kStopEventNamePrefix) + std::to_wstring(pid);

    HANDLE event_handle = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
    if (event_handle == nullptr) {
        std::wcerr << L"OpenEvent failed for '" << event_name
                   << L"'. Is FrameWatch running in pid " << pid << L"?\n";
        return false;
    }

    const BOOL signaled = SetEvent(event_handle);
    CloseHandle(event_handle);

    if (!signaled) {
        std::wcerr << L"SetEvent failed.\n";
        return false;
    }

    std::wcout << L"Shutdown signal sent to pid " << pid << L'\n';
    std::wcout << L"Status file: " << status_path.wstring() << L'\n';

    if (!WaitForEjectStatus(status_path, wait_ms)) {
        return false;
    }

    std::cout << "Eject complete.\n";
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool args_ok = true;
    InjectOptions options = ParseArgs(argc, argv, args_ok);
    if (!args_ok) {
        PrintUsage();
        return EXIT_FAILURE;
    }

    if (options.list_windows) {
        PrintWindows(EnumerateWindows());
        return EXIT_SUCCESS;
    }

    if (options.eject) {
        if (options.pid == 0) {
            if (options.window_title.empty()) {
                PrintUsage();
                return EXIT_FAILURE;
            }
            const std::optional<DWORD> resolved_pid = ResolvePidByWindowTitle(options.window_title);
            if (!resolved_pid.has_value()) {
                return EXIT_FAILURE;
            }
            options.pid = *resolved_pid;
        }

        const std::filesystem::path status_path =
            ResolveStatusPath(std::filesystem::absolute(options.dll_path));

        if (!std::filesystem::exists(options.dll_path)) {
            std::wcerr << L"Warning: DLL not found at " << options.dll_path.wstring()
                       << L". Status file path may be incorrect.\n";
        }

        if (!EjectLibrary(options.pid, status_path, options.wait_ms)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (options.pid == 0) {
        if (options.window_title.empty()) {
            PrintUsage();
            return EXIT_FAILURE;
        }

        const std::optional<DWORD> resolved_pid = ResolvePidByWindowTitle(options.window_title);
        if (!resolved_pid.has_value()) {
            return EXIT_FAILURE;
        }
        options.pid = *resolved_pid;
    }

    if (!std::filesystem::exists(options.dll_path)) {
        std::wcerr << L"DLL was not found: " << options.dll_path.wstring() << L'\n';
        return EXIT_FAILURE;
    }

    const std::filesystem::path status_path =
        ResolveStatusPath(std::filesystem::absolute(options.dll_path));
    std::error_code remove_error;
    std::filesystem::remove(status_path, remove_error);

    std::wcout << L"Injecting " << std::filesystem::absolute(options.dll_path).wstring()
               << L" into pid " << options.pid << L'\n';
    std::wcout << L"Status file: " << status_path.wstring() << L'\n';

    if (!InjectLibrary(options.pid, options.dll_path)) {
        return EXIT_FAILURE;
    }

    if (!WaitForBootstrapStatus(status_path, options.wait_ms)) {
        return EXIT_FAILURE;
    }

    std::cout << "Injection complete.\n";
    return EXIT_SUCCESS;
}
