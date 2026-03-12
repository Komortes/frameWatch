#include "framewatch/platform/window_targeting.h"

namespace framewatch {

bool WindowTargetingSupported() noexcept {
    return false;
}

std::vector<DesktopWindowInfo> EnumerateDesktopWindows() {
    return {};
}

std::optional<DesktopWindowInfo> FindDesktopWindowByTitle(std::string_view) {
    return std::nullopt;
}

std::optional<DesktopWindowInfo> FindTopDesktopWindow(std::string_view) {
    return std::nullopt;
}

}  // namespace framewatch
