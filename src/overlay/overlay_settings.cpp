#include "framewatch/overlay/overlay_settings.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "nlohmann/json.hpp"

namespace framewatch {

namespace {

int ClampWindowWidth(int width) noexcept {
    return std::max(width, 640);
}

int ClampWindowHeight(int height) noexcept {
    return std::max(height, 420);
}

// Helpers to safely extract optional typed values from a parsed JSON object.
template <typename T>
std::optional<T> GetOpt(const nlohmann::json& j, std::string_view key) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return std::nullopt;
    }
    try {
        return it->get<T>();
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

double ClampOverlayOpacity(double opacity) noexcept {
    return std::clamp(opacity, 0.35, 1.0);
}

void AdjustOverlayOpacity(OverlaySettings& settings, double delta) noexcept {
    settings.panel_opacity = ClampOverlayOpacity(settings.panel_opacity + delta);
}

int ClampTargetFps(int fps) noexcept {
    return std::clamp(fps, 10, 1'000);
}

int CycleTargetFps(int fps, int direction) noexcept {
    constexpr std::array<int, 7> kPresets{30, 60, 90, 120, 144, 165, 240};

    std::size_t index = 0;
    int best_distance = std::abs(kPresets[0] - fps);
    for (std::size_t i = 1; i < kPresets.size(); ++i) {
        const int distance = std::abs(kPresets[i] - fps);
        if (distance < best_distance) {
            best_distance = distance;
            index = i;
        }
    }

    const int count = static_cast<int>(kPresets.size());
    const int stepped = (static_cast<int>(index) + (direction % count) + count) % count;
    return kPresets[static_cast<std::size_t>(stepped)];
}

OverlayDockAnchor CycleOverlayDockAnchor(OverlayDockAnchor anchor, int direction) noexcept {
    constexpr int kAnchorCount = 4;
    int index = 0;

    switch (anchor) {
        case OverlayDockAnchor::RightTop:   index = 0; break;
        case OverlayDockAnchor::RightBottom: index = 1; break;
        case OverlayDockAnchor::LeftTop:    index = 2; break;
        case OverlayDockAnchor::LeftBottom: index = 3; break;
    }

    index = (index + (direction % kAnchorCount) + kAnchorCount) % kAnchorCount;

    switch (index) {
        case 0: return OverlayDockAnchor::RightTop;
        case 1: return OverlayDockAnchor::RightBottom;
        case 2: return OverlayDockAnchor::LeftTop;
        default: return OverlayDockAnchor::LeftBottom;
    }
}

std::string_view OverlayDockAnchorName(OverlayDockAnchor anchor) noexcept {
    switch (anchor) {
        case OverlayDockAnchor::RightTop:    return "RIGHT TOP";
        case OverlayDockAnchor::RightBottom: return "RIGHT BOTTOM";
        case OverlayDockAnchor::LeftTop:     return "LEFT TOP";
        case OverlayDockAnchor::LeftBottom:  return "LEFT BOTTOM";
    }
    return "RIGHT TOP";
}

std::optional<OverlayDockAnchor> ParseOverlayDockAnchor(std::string_view value) noexcept {
    if (value == "RIGHT TOP")    return OverlayDockAnchor::RightTop;
    if (value == "RIGHT BOTTOM") return OverlayDockAnchor::RightBottom;
    if (value == "LEFT TOP")     return OverlayDockAnchor::LeftTop;
    if (value == "LEFT BOTTOM")  return OverlayDockAnchor::LeftBottom;
    return std::nullopt;
}

std::optional<OverlaySettings> LoadOverlaySettings(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        input >> j;
    } catch (...) {
        return std::nullopt;
    }

    if (!j.is_object()) {
        return std::nullopt;
    }

    OverlaySettings settings;

    if (auto v = GetOpt<bool>(j, "show_overlay"))                  settings.show_overlay = *v;
    if (auto v = GetOpt<bool>(j, "show_graph"))                    settings.show_graph = *v;
    if (auto v = GetOpt<bool>(j, "show_sidebar"))                  settings.show_sidebar = *v;
    if (auto v = GetOpt<bool>(j, "show_hotkey_hints"))             settings.show_hotkey_hints = *v;
    if (auto v = GetOpt<bool>(j, "show_settings_panel"))           settings.show_settings_panel = *v;
    if (auto v = GetOpt<bool>(j, "capture_input_when_panel_open")) settings.capture_input_when_panel_open = *v;
    if (auto v = GetOpt<bool>(j, "compact_mode"))                  settings.compact_mode = *v;
    if (auto v = GetOpt<bool>(j, "follow_target_window"))          settings.follow_target_window = *v;

    if (auto v = GetOpt<double>(j, "panel_opacity")) settings.panel_opacity = ClampOverlayOpacity(*v);
    if (auto v = GetOpt<int>(j, "target_fps"))       settings.target_fps = ClampTargetFps(*v);
    if (auto v = GetOpt<int>(j, "window_width"))     settings.window_width = ClampWindowWidth(*v);
    if (auto v = GetOpt<int>(j, "window_height"))    settings.window_height = ClampWindowHeight(*v);

    if (auto v = GetOpt<std::string>(j, "dock_anchor")) {
        if (auto parsed = ParseOverlayDockAnchor(*v)) {
            settings.dock_anchor = *parsed;
        }
    }
    if (auto v = GetOpt<std::string>(j, "target_window_query")) {
        settings.target_window_query = *v;
    }

    settings.window_x = GetOpt<int>(j, "window_x");
    settings.window_y = GetOpt<int>(j, "window_y");

    return settings;
}

bool SaveOverlaySettings(const OverlaySettings& settings, const std::filesystem::path& path) {
    std::error_code create_error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), create_error);
        if (create_error) {
            return false;
        }
    }

    nlohmann::json j;
    j["show_overlay"]                  = settings.show_overlay;
    j["show_graph"]                    = settings.show_graph;
    j["show_sidebar"]                  = settings.show_sidebar;
    j["show_hotkey_hints"]             = settings.show_hotkey_hints;
    j["show_settings_panel"]           = settings.show_settings_panel;
    j["capture_input_when_panel_open"] = settings.capture_input_when_panel_open;
    j["compact_mode"]                  = settings.compact_mode;
    j["panel_opacity"]                 = ClampOverlayOpacity(settings.panel_opacity);
    j["target_fps"]                    = ClampTargetFps(settings.target_fps);
    j["dock_anchor"]                   = std::string(OverlayDockAnchorName(settings.dock_anchor));
    j["follow_target_window"]          = settings.follow_target_window;
    j["target_window_query"]           = settings.target_window_query;
    j["window_width"]                  = ClampWindowWidth(settings.window_width);
    j["window_height"]                 = ClampWindowHeight(settings.window_height);
    j["window_x"] = settings.window_x.has_value() ? nlohmann::json(*settings.window_x)
                                                   : nlohmann::json(nullptr);
    j["window_y"] = settings.window_y.has_value() ? nlohmann::json(*settings.window_y)
                                                   : nlohmann::json(nullptr);

    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    output << j.dump(2) << '\n';
    return output.good();
}

}  // namespace framewatch
