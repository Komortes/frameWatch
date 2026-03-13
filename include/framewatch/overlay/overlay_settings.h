#pragma once

#include <string_view>

namespace framewatch {

enum class OverlayDockAnchor {
    RightTop,
    RightBottom,
    LeftTop,
    LeftBottom,
};

struct OverlaySettings {
    bool show_graph{true};
    bool show_sidebar{true};
    double panel_opacity{0.86};
    OverlayDockAnchor dock_anchor{OverlayDockAnchor::RightTop};
};

double ClampOverlayOpacity(double opacity) noexcept;
void AdjustOverlayOpacity(OverlaySettings& settings, double delta) noexcept;
OverlayDockAnchor CycleOverlayDockAnchor(OverlayDockAnchor anchor, int direction = 1) noexcept;
std::string_view OverlayDockAnchorName(OverlayDockAnchor anchor) noexcept;

}  // namespace framewatch
