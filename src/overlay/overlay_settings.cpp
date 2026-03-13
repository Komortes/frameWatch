#include "framewatch/overlay/overlay_settings.h"

#include <algorithm>

namespace framewatch {

double ClampOverlayOpacity(double opacity) noexcept {
    return std::clamp(opacity, 0.35, 1.0);
}

void AdjustOverlayOpacity(OverlaySettings& settings, double delta) noexcept {
    settings.panel_opacity = ClampOverlayOpacity(settings.panel_opacity + delta);
}

OverlayDockAnchor CycleOverlayDockAnchor(OverlayDockAnchor anchor, int direction) noexcept {
    constexpr int kAnchorCount = 4;
    int index = 0;

    switch (anchor) {
        case OverlayDockAnchor::RightTop:
            index = 0;
            break;
        case OverlayDockAnchor::RightBottom:
            index = 1;
            break;
        case OverlayDockAnchor::LeftTop:
            index = 2;
            break;
        case OverlayDockAnchor::LeftBottom:
            index = 3;
            break;
    }

    index = (index + (direction % kAnchorCount) + kAnchorCount) % kAnchorCount;

    switch (index) {
        case 0:
            return OverlayDockAnchor::RightTop;
        case 1:
            return OverlayDockAnchor::RightBottom;
        case 2:
            return OverlayDockAnchor::LeftTop;
        default:
            return OverlayDockAnchor::LeftBottom;
    }
}

std::string_view OverlayDockAnchorName(OverlayDockAnchor anchor) noexcept {
    switch (anchor) {
        case OverlayDockAnchor::RightTop:
            return "RIGHT TOP";
        case OverlayDockAnchor::RightBottom:
            return "RIGHT BOTTOM";
        case OverlayDockAnchor::LeftTop:
            return "LEFT TOP";
        case OverlayDockAnchor::LeftBottom:
            return "LEFT BOTTOM";
    }

    return "RIGHT TOP";
}

}  // namespace framewatch
