#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace framewatch {

enum class OverlayDockAnchor {
    RightTop,
    RightBottom,
    LeftTop,
    LeftBottom,
};

struct OverlaySettings {
    bool show_overlay{true};
    bool show_graph{true};
    bool show_sidebar{true};
    double panel_opacity{0.86};
    OverlayDockAnchor dock_anchor{OverlayDockAnchor::RightTop};
    bool follow_target_window{false};
    std::string target_window_query;
    int window_width{1180};
    int window_height{760};
    std::optional<int> window_x;
    std::optional<int> window_y;
};

double ClampOverlayOpacity(double opacity) noexcept;
void AdjustOverlayOpacity(OverlaySettings& settings, double delta) noexcept;
OverlayDockAnchor CycleOverlayDockAnchor(OverlayDockAnchor anchor, int direction = 1) noexcept;
std::string_view OverlayDockAnchorName(OverlayDockAnchor anchor) noexcept;
std::optional<OverlayDockAnchor> ParseOverlayDockAnchor(std::string_view value) noexcept;

std::optional<OverlaySettings> LoadOverlaySettings(const std::filesystem::path& path);
bool SaveOverlaySettings(const OverlaySettings& settings, const std::filesystem::path& path);

}  // namespace framewatch
