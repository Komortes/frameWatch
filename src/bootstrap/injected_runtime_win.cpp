#include "framewatch/bootstrap/injected_runtime.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "framewatch/hooks/hook_overlay_service.h"

namespace {

struct InjectedBootstrapContext {
    HMODULE module{nullptr};
    HANDLE stop_event{nullptr};
    std::unique_ptr<framewatch::HookOverlayService> service;
    std::filesystem::path module_path;
    std::filesystem::path output_directory;
    std::filesystem::path status_path;
    std::atomic<framewatch::InjectedRuntimeState> state{
        framewatch::InjectedRuntimeState::Uninitialized};
};

InjectedBootstrapContext& BootstrapContext() {
    static InjectedBootstrapContext context;
    return context;
}

std::filesystem::path ResolveModulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(),
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

void WriteStatusFile(framewatch::InjectedRuntimeState state, std::string_view message) {
    InjectedBootstrapContext& context = BootstrapContext();
    if (context.status_path.empty()) {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(context.output_directory, error);

    std::ofstream output(context.status_path, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }

    output << "state=" << framewatch::InjectedRuntimeStateName(state) << '\n';
    output << "pid=" << GetCurrentProcessId() << '\n';
    if (!context.module_path.empty()) {
        output << "module=" << context.module_path.string() << '\n';
    }
    if (!message.empty()) {
        output << "message=" << message << '\n';
    }
    if (context.service) {
        output << "hook=" << context.service->HookDescription() << '\n';
        output << "renderer=" << context.service->Runtime().RendererName() << '\n';
    }
}

DWORD WINAPI BootstrapThreadMain(void*) {
    InjectedBootstrapContext& context = BootstrapContext();
    context.state.store(framewatch::InjectedRuntimeState::Starting, std::memory_order_release);

    context.module_path = ResolveModulePath(context.module);
    if (!context.module_path.empty()) {
        context.output_directory =
            context.module_path.parent_path() / std::string(framewatch::kInjectedStatusDirectory);
        context.status_path =
            context.output_directory / std::string(framewatch::kInjectedStatusFileName);
    }

    WriteStatusFile(framewatch::InjectedRuntimeState::Starting, "bootstrapping");

    context.service = framewatch::CreateHookOverlayService(360);
    if (!context.service) {
        context.state.store(framewatch::InjectedRuntimeState::Failed, std::memory_order_release);
        WriteStatusFile(framewatch::InjectedRuntimeState::Failed, "failed to allocate hook service");
        FreeLibraryAndExitThread(context.module, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    if (!context.output_directory.empty()) {
        context.service->Runtime().SetExportPaths(
            context.output_directory / "framewatch_injected_session.csv",
            context.output_directory / "framewatch_injected_session.json");
    }

    if (!context.service->Initialize()) {
        context.state.store(framewatch::InjectedRuntimeState::Failed, std::memory_order_release);
        WriteStatusFile(framewatch::InjectedRuntimeState::Failed,
                        std::string(context.service->HookDescription()));
        context.service.reset();
        FreeLibraryAndExitThread(context.module, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    context.state.store(framewatch::InjectedRuntimeState::Running, std::memory_order_release);
    WriteStatusFile(framewatch::InjectedRuntimeState::Running, "overlay active");

    if (context.stop_event != nullptr) {
        WaitForSingleObject(context.stop_event, INFINITE);
    }

    context.state.store(framewatch::InjectedRuntimeState::Stopping, std::memory_order_release);
    WriteStatusFile(framewatch::InjectedRuntimeState::Stopping, "shutdown requested");

    if (context.service) {
        context.service->Shutdown();
        context.service.reset();
    }

    context.state.store(framewatch::InjectedRuntimeState::Uninitialized, std::memory_order_release);
    WriteStatusFile(framewatch::InjectedRuntimeState::Uninitialized, "stopped");

    HANDLE stop_event = context.stop_event;
    context.stop_event = nullptr;
    if (stop_event != nullptr) {
        CloseHandle(stop_event);
    }

    FreeLibraryAndExitThread(context.module, EXIT_SUCCESS);
    return EXIT_SUCCESS;
}

}  // namespace

extern "C" int FrameWatchInjectedRuntimeStateCode() {
    return static_cast<int>(BootstrapContext().state.load(std::memory_order_acquire));
}

extern "C" int FrameWatchRequestShutdown() {
    InjectedBootstrapContext& context = BootstrapContext();
    if (context.stop_event == nullptr) {
        return 0;
    }
    return SetEvent(context.stop_event) != 0 ? 1 : 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    InjectedBootstrapContext& context = BootstrapContext();

    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(module);
            context.module = module;
            const std::wstring stop_event_name =
                std::wstring(framewatch::kStopEventNamePrefix) + std::to_wstring(GetCurrentProcessId());
            context.stop_event = CreateEventW(nullptr, TRUE, FALSE, stop_event_name.c_str());
            const DWORD create_event_error = GetLastError();
            if (context.stop_event == nullptr) {
                context.state.store(framewatch::InjectedRuntimeState::Failed,
                                    std::memory_order_release);
                return FALSE;
            }
            if (create_event_error == ERROR_ALREADY_EXISTS) {
                CloseHandle(context.stop_event);
                context.stop_event = nullptr;
                context.state.store(framewatch::InjectedRuntimeState::Failed,
                                    std::memory_order_release);
                return FALSE;
            }

            HANDLE thread_handle = CreateThread(
                nullptr, 0, &BootstrapThreadMain, nullptr, 0, nullptr);
            if (thread_handle == nullptr) {
                CloseHandle(context.stop_event);
                context.stop_event = nullptr;
                context.state.store(framewatch::InjectedRuntimeState::Failed,
                                    std::memory_order_release);
                return FALSE;
            }

            CloseHandle(thread_handle);
            return TRUE;
        }
        case DLL_PROCESS_DETACH:
            if (context.stop_event != nullptr) {
                SetEvent(context.stop_event);
            }
            return TRUE;
        default:
            return TRUE;
    }
}
