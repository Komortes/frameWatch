#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace framewatch {

struct DesktopWindowInfo {
    std::uint64_t id{0};
    std::string owner_name;
    std::string title;
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

bool WindowTargetingSupported() noexcept;
std::vector<DesktopWindowInfo> EnumerateDesktopWindows();
std::optional<DesktopWindowInfo> FindDesktopWindowByTitle(std::string_view title_substring);
std::optional<DesktopWindowInfo> FindTopDesktopWindow(std::string_view exclude_title_substring = {});

}  // namespace framewatch
