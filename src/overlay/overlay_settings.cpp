#include "framewatch/overlay/overlay_settings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <system_error>

namespace framewatch {

namespace {

std::string Trim(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

std::optional<std::string> ExtractJsonToken(std::string_view source, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = source.find(pattern);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t value_pos = source.find(':', key_pos + pattern.size());
    if (value_pos == std::string_view::npos) {
        return std::nullopt;
    }

    ++value_pos;
    while (value_pos < source.size() &&
           std::isspace(static_cast<unsigned char>(source[value_pos])) != 0) {
        ++value_pos;
    }

    if (value_pos >= source.size()) {
        return std::nullopt;
    }

    if (source[value_pos] == '"') {
        std::string decoded;
        bool escape = false;

        for (std::size_t cursor = value_pos + 1; cursor < source.size(); ++cursor) {
            const char ch = source[cursor];
            if (escape) {
                switch (ch) {
                    case '\\':
                        decoded.push_back('\\');
                        break;
                    case '"':
                        decoded.push_back('"');
                        break;
                    case 'n':
                        decoded.push_back('\n');
                        break;
                    case 'r':
                        decoded.push_back('\r');
                        break;
                    case 't':
                        decoded.push_back('\t');
                        break;
                    default:
                        decoded.push_back(ch);
                        break;
                }
                escape = false;
                continue;
            }

            if (ch == '\\') {
                escape = true;
                continue;
            }

            if (ch == '"') {
                return decoded;
            }

            decoded.push_back(ch);
        }

        return std::nullopt;
    }

    std::size_t end_pos = value_pos;
    while (end_pos < source.size() && source[end_pos] != ',' && source[end_pos] != '}' &&
           source[end_pos] != '\n' && source[end_pos] != '\r') {
        ++end_pos;
    }

    return Trim(source.substr(value_pos, end_pos - value_pos));
}

std::optional<bool> ExtractJsonBool(std::string_view source, std::string_view key) {
    const auto token = ExtractJsonToken(source, key);
    if (!token.has_value()) {
        return std::nullopt;
    }
    if (*token == "true") {
        return true;
    }
    if (*token == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> ExtractJsonNumber(std::string_view source, std::string_view key) {
    const auto token = ExtractJsonToken(source, key);
    if (!token.has_value()) {
        return std::nullopt;
    }

    try {
        return std::stod(*token);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> ExtractJsonInteger(std::string_view source, std::string_view key) {
    const auto token = ExtractJsonToken(source, key);
    if (!token.has_value() || *token == "null") {
        return std::nullopt;
    }

    try {
        return std::stoi(*token);
    } catch (...) {
        return std::nullopt;
    }
}

int ClampWindowWidth(int width) noexcept {
    return std::max(width, 640);
}

int ClampWindowHeight(int height) noexcept {
    return std::max(height, 420);
}

std::string EscapeJsonString(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

}  // namespace

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

std::optional<OverlayDockAnchor> ParseOverlayDockAnchor(std::string_view value) noexcept {
    const std::string normalized = Trim(value);

    if (normalized == "RIGHT TOP") {
        return OverlayDockAnchor::RightTop;
    }
    if (normalized == "RIGHT BOTTOM") {
        return OverlayDockAnchor::RightBottom;
    }
    if (normalized == "LEFT TOP") {
        return OverlayDockAnchor::LeftTop;
    }
    if (normalized == "LEFT BOTTOM") {
        return OverlayDockAnchor::LeftBottom;
    }

    return std::nullopt;
}

std::optional<OverlaySettings> LoadOverlaySettings(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    if (content.empty()) {
        return OverlaySettings{};
    }

    OverlaySettings settings;
    if (const auto show_overlay = ExtractJsonBool(content, "show_overlay")) {
        settings.show_overlay = *show_overlay;
    }
    if (const auto show_graph = ExtractJsonBool(content, "show_graph")) {
        settings.show_graph = *show_graph;
    }
    if (const auto show_sidebar = ExtractJsonBool(content, "show_sidebar")) {
        settings.show_sidebar = *show_sidebar;
    }
    if (const auto show_hotkey_hints = ExtractJsonBool(content, "show_hotkey_hints")) {
        settings.show_hotkey_hints = *show_hotkey_hints;
    }
    if (const auto show_settings_panel = ExtractJsonBool(content, "show_settings_panel")) {
        settings.show_settings_panel = *show_settings_panel;
    }
    if (const auto compact_mode = ExtractJsonBool(content, "compact_mode")) {
        settings.compact_mode = *compact_mode;
    }
    if (const auto panel_opacity = ExtractJsonNumber(content, "panel_opacity")) {
        settings.panel_opacity = ClampOverlayOpacity(*panel_opacity);
    }
    if (const auto dock_anchor = ExtractJsonToken(content, "dock_anchor")) {
        if (const auto parsed = ParseOverlayDockAnchor(*dock_anchor)) {
            settings.dock_anchor = *parsed;
        }
    }
    if (const auto follow_target = ExtractJsonBool(content, "follow_target_window")) {
        settings.follow_target_window = *follow_target;
    }
    if (const auto target_query = ExtractJsonToken(content, "target_window_query")) {
        settings.target_window_query = *target_query;
    }
    if (const auto window_width = ExtractJsonInteger(content, "window_width")) {
        settings.window_width = ClampWindowWidth(*window_width);
    }
    if (const auto window_height = ExtractJsonInteger(content, "window_height")) {
        settings.window_height = ClampWindowHeight(*window_height);
    }
    settings.window_x = ExtractJsonInteger(content, "window_x");
    settings.window_y = ExtractJsonInteger(content, "window_y");

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

    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    output << std::boolalpha << std::fixed << std::setprecision(2);
    output << "{\n";
    output << "  \"show_overlay\": " << settings.show_overlay << ",\n";
    output << "  \"show_graph\": " << settings.show_graph << ",\n";
    output << "  \"show_sidebar\": " << settings.show_sidebar << ",\n";
    output << "  \"show_hotkey_hints\": " << settings.show_hotkey_hints << ",\n";
    output << "  \"show_settings_panel\": " << settings.show_settings_panel << ",\n";
    output << "  \"compact_mode\": " << settings.compact_mode << ",\n";
    output << "  \"panel_opacity\": " << settings.panel_opacity << ",\n";
    output << "  \"dock_anchor\": \"" << OverlayDockAnchorName(settings.dock_anchor) << "\",\n";
    output << "  \"follow_target_window\": " << settings.follow_target_window << ",\n";
    output << "  \"target_window_query\": \""
           << EscapeJsonString(settings.target_window_query) << "\",\n";
    output << "  \"window_width\": " << ClampWindowWidth(settings.window_width) << ",\n";
    output << "  \"window_height\": " << ClampWindowHeight(settings.window_height) << ",\n";
    output << "  \"window_x\": "
           << (settings.window_x.has_value() ? std::to_string(*settings.window_x) : "null")
           << ",\n";
    output << "  \"window_y\": "
           << (settings.window_y.has_value() ? std::to_string(*settings.window_y) : "null")
           << '\n';
    output << "}\n";

    return output.good();
}

}  // namespace framewatch
