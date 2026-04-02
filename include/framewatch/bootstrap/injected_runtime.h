#pragma once

#include <string_view>

namespace framewatch {

enum class InjectedRuntimeState : int {
    Uninitialized = 0,
    Starting = 1,
    Running = 2,
    Failed = 3,
    Stopping = 4,
};

inline constexpr std::string_view kInjectedStatusDirectory = "output";
inline constexpr std::string_view kInjectedStatusFileName = "framewatch_injected_status.txt";

#if defined(_WIN32)
inline constexpr std::wstring_view kStopEventNamePrefix = L"FrameWatch_Stop_";
#endif

constexpr std::string_view InjectedRuntimeStateName(InjectedRuntimeState state) noexcept {
    switch (state) {
        case InjectedRuntimeState::Uninitialized:
            return "uninitialized";
        case InjectedRuntimeState::Starting:
            return "starting";
        case InjectedRuntimeState::Running:
            return "running";
        case InjectedRuntimeState::Failed:
            return "failed";
        case InjectedRuntimeState::Stopping:
            return "stopping";
    }
    return "unknown";
}

#if defined(_WIN32)
#if defined(FRAMEWATCH_BUILD_INJECTED_RUNTIME)
#define FRAMEWATCH_INJECTED_RUNTIME_API __declspec(dllexport)
#else
#define FRAMEWATCH_INJECTED_RUNTIME_API __declspec(dllimport)
#endif
#else
#define FRAMEWATCH_INJECTED_RUNTIME_API
#endif

}  // namespace framewatch

extern "C" {
FRAMEWATCH_INJECTED_RUNTIME_API int FrameWatchInjectedRuntimeStateCode();
FRAMEWATCH_INJECTED_RUNTIME_API int FrameWatchRequestShutdown();
}
