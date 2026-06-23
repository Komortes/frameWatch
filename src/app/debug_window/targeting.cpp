#include "targeting.h"

#include <algorithm>
#include <cctype>

#include "framewatch/platform/window_targeting.h"

namespace dw {

std::string ToLower(std::string_view input) {
    std::string lowered;
    lowered.reserve(input.size());
    for (const unsigned char ch : input) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool ContainsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) return true;
    return ToLower(text).find(ToLower(needle)) != std::string::npos;
}

std::string SanitizeUiText(std::string_view text, std::size_t max_chars) {
    std::string output;
    output.reserve(std::min(max_chars, text.size()));
    for (const unsigned char ch : text) {
        if (output.size() >= max_chars) break;
        output.push_back((ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '?');
    }
    if (text.size() > max_chars && output.size() >= 3) {
        output.resize(max_chars - 3);
        output += "...";
    }
    return output;
}

std::string TrimWhitespace(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

std::string TargetLabel(const framewatch::DesktopWindowInfo& window) {
    const std::string owner = SanitizeUiText(window.owner_name, 14);
    const std::string title = SanitizeUiText(window.title, 18);
    if (owner.empty()) return title;
    if (title.empty()) return owner;
    return owner + "/" + title;
}

std::string TargetQueryForPersistence(const framewatch::DesktopWindowInfo& window) {
    return !window.title.empty() ? window.title : window.owner_name;
}

bool IsSelfWindow(const framewatch::DesktopWindowInfo& window, std::string_view marker) {
    return ContainsIgnoreCase(window.title, marker) || ContainsIgnoreCase(window.owner_name, marker);
}

void RefreshTargets(TargetingState& targeting, std::string_view self_marker) {
    if (!targeting.supported) return;

    const std::uint64_t previous_id =
        (targeting.selected_index >= 0 &&
         targeting.selected_index < static_cast<int>(targeting.windows.size()))
            ? targeting.windows[static_cast<std::size_t>(targeting.selected_index)].id
            : 0;

    std::vector<framewatch::DesktopWindowInfo> windows = framewatch::EnumerateDesktopWindows();
    windows.erase(std::remove_if(windows.begin(), windows.end(),
                                 [&](const framewatch::DesktopWindowInfo& w) {
                                     return IsSelfWindow(w, self_marker);
                                 }),
                  windows.end());

    targeting.windows = std::move(windows);
    targeting.selected_index = -1;

    if (previous_id != 0) {
        for (std::size_t i = 0; i < targeting.windows.size(); ++i) {
            if (targeting.windows[i].id == previous_id) {
                targeting.selected_index = static_cast<int>(i);
                return;
            }
        }
    }

    if (!targeting.title_query.empty()) {
        for (std::size_t i = 0; i < targeting.windows.size(); ++i) {
            if (ContainsIgnoreCase(targeting.windows[i].title, targeting.title_query) ||
                ContainsIgnoreCase(targeting.windows[i].owner_name, targeting.title_query)) {
                targeting.selected_index = static_cast<int>(i);
                return;
            }
        }
    }
}

void PickFrontmostTarget(TargetingState& targeting, std::string_view self_marker) {
    RefreshTargets(targeting, self_marker);
    if (!targeting.windows.empty()) {
        targeting.selected_index = 0;
    }
}

void CycleTarget(TargetingState& targeting, int direction) {
    if (targeting.windows.empty()) {
        targeting.selected_index = -1;
        return;
    }
    if (targeting.selected_index < 0) {
        targeting.selected_index = direction >= 0 ? 0 : static_cast<int>(targeting.windows.size()) - 1;
        return;
    }
    const int size = static_cast<int>(targeting.windows.size());
    targeting.selected_index = (targeting.selected_index + direction + size) % size;
}

std::optional<framewatch::DesktopWindowInfo> CurrentTarget(const TargetingState& targeting) {
    if (targeting.selected_index < 0 ||
        targeting.selected_index >= static_cast<int>(targeting.windows.size())) {
        return std::nullopt;
    }
    return targeting.windows[static_cast<std::size_t>(targeting.selected_index)];
}

void DockWindowToTarget(SDL_Window* window,
                        const framewatch::DesktopWindowInfo& target,
                        const framewatch::OverlaySettings& settings) {
    int window_width = 0, window_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);

    SDL_Rect usable_bounds{};
    SDL_GetDisplayUsableBounds(0, &usable_bounds);

    const bool prefer_right =
        settings.dock_anchor == framewatch::OverlayDockAnchor::RightTop ||
        settings.dock_anchor == framewatch::OverlayDockAnchor::RightBottom;
    const bool align_bottom =
        settings.dock_anchor == framewatch::OverlayDockAnchor::RightBottom ||
        settings.dock_anchor == framewatch::OverlayDockAnchor::LeftBottom;

    int x = prefer_right ? target.x + target.width + 18 : target.x - window_width - 18;
    if (prefer_right && x + window_width > usable_bounds.x + usable_bounds.w) {
        x = target.x - window_width - 18;
    } else if (!prefer_right && x < usable_bounds.x) {
        x = target.x + target.width + 18;
    }
    x = std::clamp(x, usable_bounds.x, usable_bounds.x + usable_bounds.w - window_width);

    const int preferred_y = align_bottom ? target.y + target.height - window_height : target.y;
    const int y = std::clamp(preferred_y, usable_bounds.y,
                             usable_bounds.y + usable_bounds.h - window_height);

    SDL_SetWindowPosition(window, x, y);
}

}  // namespace dw
