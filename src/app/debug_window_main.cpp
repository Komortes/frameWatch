#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "framewatch/overlay/overlay_settings.h"
#include "framewatch/platform/window_targeting.h"
#include "framewatch/session/performance_session.h"

namespace {

using SteadyClock = std::chrono::steady_clock;
using Glyph = std::array<std::uint8_t, 7>;

constexpr std::string_view kSelfTitleMarker = "FrameWatch Mini Debug Window";

struct AppOptions {
    bool smoke_test{false};
    bool list_targets{false};
    bool follow_target{false};
    std::string target_title;
    std::filesystem::path csv_path{"output/framewatch_debug_window.csv"};
    std::filesystem::path json_path{"output/framewatch_debug_window.json"};
    std::filesystem::path settings_path{"output/framewatch_debug_window_settings.json"};
};

struct Palette {
    SDL_Color background_top{9, 14, 23, 255};
    SDL_Color background_bottom{22, 30, 43, 255};
    SDL_Color panel{20, 27, 39, 220};
    SDL_Color panel_border{74, 89, 109, 255};
    SDL_Color text_primary{232, 239, 247, 255};
    SDL_Color text_muted{134, 147, 166, 255};
    SDL_Color accent{69, 207, 164, 255};
    SDL_Color accent_2{96, 165, 250, 255};
    SDL_Color warning{245, 158, 11, 255};
    SDL_Color danger{248, 113, 113, 255};
    SDL_Color grid{55, 65, 81, 255};
};

struct TargetingState {
    bool supported{framewatch::WindowTargetingSupported()};
    bool follow_enabled{false};
    std::string title_query;
    std::vector<framewatch::DesktopWindowInfo> windows;
    int selected_index{-1};
};

struct SyntheticFrameGenerator {
    std::mt19937 rng{42};
    std::normal_distribution<double> baseline_frametime_ms{16.6, 0.35};
    std::uint64_t generated_frames{0};

    void Reset() {
        rng.seed(42);
        generated_frames = 0;
    }

    double NextFrametimeMs() {
        ++generated_frames;

        double frametime_ms = std::max(5.0, baseline_frametime_ms(rng));
        if (generated_frames % 120 == 0) {
            frametime_ms += 8.5;
        }
        if (generated_frames % 257 == 0) {
            frametime_ms += 16.0;
        }

        return frametime_ms;
    }
};

struct WindowState {
    bool running{true};
    bool quit{false};
    bool show_settings_panel{false};
    bool editing_target_query{false};
    int target_list_start_index{0};
    std::string target_query_buffer;
    std::string status_text{"SIMULATION RUNNING"};
    SteadyClock::time_point status_until{};
    SteadyClock::time_point last_step_at{SteadyClock::now()};
    SteadyClock::time_point last_title_update_at{SteadyClock::now()};
    SteadyClock::time_point last_target_refresh_at{SteadyClock::now() - std::chrono::seconds(1)};
    SteadyClock::time_point last_follow_sync_at{SteadyClock::now() - std::chrono::seconds(1)};
};

enum class SettingsPanelAction {
    ToggleGraph,
    ToggleSidebar,
    ToggleFollow,
    CycleDock,
    OpacityDown,
    OpacityUp,
    TargetNext,
    TargetFront,
    ClearTarget,
    ResetDefaults,
};

struct SettingsPanelButton {
    SDL_Rect rect{};
    SettingsPanelAction action{SettingsPanelAction::ToggleGraph};
    std::string label;
    bool accent{false};
};

struct SettingsPanelTargetRow {
    SDL_Rect rect{};
    int window_index{-1};
};

struct SettingsPanelLayout {
    SDL_Rect panel_rect{};
    SDL_Rect query_rect{};
    SDL_Rect query_apply_rect{};
    SDL_Rect query_clear_rect{};
    SDL_Rect target_list_rect{};
    SDL_Rect target_page_prev_rect{};
    SDL_Rect target_page_next_rect{};
    int visible_rows{0};
    int first_visible_index{0};
    bool has_prev_page{false};
    bool has_next_page{false};
    std::vector<SettingsPanelButton> buttons;
    std::vector<SettingsPanelTargetRow> target_rows;
};

constexpr int kSettingsPanelMargin = 16;
constexpr int kSettingsMaxVisibleRows = 6;
constexpr int kSettingsButtonGap = 6;
constexpr int kSettingsButtonHeight = 24;
constexpr int kSettingsButtonStartY = 84;
constexpr int kSettingsTargetListHeaderHeight = 22;
constexpr int kSettingsTargetRowStride = 22;
constexpr int kSettingsInfoHeight = 54;
constexpr int kSettingsBottomPadding = 16;

AppOptions ParseArgs(int argc, char** argv) {
    AppOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke-test") {
            options.smoke_test = true;
        } else if (arg == "--list-targets") {
            options.list_targets = true;
        } else if (arg == "--follow-target") {
            options.follow_target = true;
        } else if (arg == "--target-title" && (i + 1) < argc) {
            options.target_title = argv[++i];
        } else if (arg == "--csv" && (i + 1) < argc) {
            options.csv_path = argv[++i];
        } else if (arg == "--json" && (i + 1) < argc) {
            options.json_path = argv[++i];
        } else if (arg == "--settings" && (i + 1) < argc) {
            options.settings_path = argv[++i];
        }
    }

    return options;
}

std::string ToLower(std::string_view input) {
    std::string lowered;
    lowered.reserve(input.size());
    for (const unsigned char ch : input) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool ContainsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }

    const std::string lowered_text = ToLower(text);
    const std::string lowered_needle = ToLower(needle);
    return lowered_text.find(lowered_needle) != std::string::npos;
}

std::string SanitizeUiText(std::string_view text, std::size_t max_chars = 30) {
    std::string output;
    output.reserve(std::min(max_chars, text.size()));

    for (const unsigned char ch : text) {
        if (output.size() >= max_chars) {
            break;
        }

        if (ch >= 32 && ch <= 126) {
            output.push_back(static_cast<char>(ch));
        } else {
            output.push_back('?');
        }
    }

    if (text.size() > max_chars && output.size() >= 3) {
        output.resize(max_chars - 3);
        output += "...";
    }

    return output;
}

std::string TrimWhitespace(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

Glyph GlyphFor(char input) {
    const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));

    switch (ch) {
        case 'A':
            return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'B':
            return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case 'C':
            return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case 'D':
            return {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
        case 'E':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case 'F':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case 'G':
            return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
        case 'H':
            return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'I':
            return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case 'K':
            return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L':
            return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case 'M':
            return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N':
            return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O':
            return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'P':
            return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case 'Q':
            return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case 'R':
            return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case 'S':
            return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case 'T':
            return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'V':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        case 'W':
            return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
        case 'X':
            return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case 'Y':
            return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case '0':
            return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1':
            return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case '2':
            return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3':
            return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case '4':
            return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5':
            return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
        case '6':
            return {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7':
            return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8':
            return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9':
            return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
        case ':':
            return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
        case '.':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
        case '%':
            return {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
        case '-':
            return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case '/':
            return {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
        case '+':
            return {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
        case '_':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
        case ' ':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        default:
            return {0x1F, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00};
    }
}

int TextWidth(std::string_view text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale;
}

void SetDrawColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void FillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SetDrawColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void DrawRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
    SetDrawColor(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

void DrawGlyph(SDL_Renderer* renderer, int x, int y, int scale, SDL_Color color, char ch) {
    const Glyph glyph = GlyphFor(ch);
    SetDrawColor(renderer, color);

    for (int row = 0; row < static_cast<int>(glyph.size()); ++row) {
        for (int col = 0; col < 5; ++col) {
            const bool filled = (glyph[static_cast<std::size_t>(row)] & (1 << (4 - col))) != 0;
            if (!filled) {
                continue;
            }

            const SDL_Rect pixel_rect{x + (col * scale), y + (row * scale), scale, scale};
            SDL_RenderFillRect(renderer, &pixel_rect);
        }
    }
}

void DrawText(SDL_Renderer* renderer,
              int x,
              int y,
              int scale,
              SDL_Color color,
              std::string_view text) {
    int cursor_x = x;
    int cursor_y = y;

    for (const char ch : text) {
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += 8 * scale;
            continue;
        }

        DrawGlyph(renderer, cursor_x, cursor_y, scale, color, ch);
        cursor_x += 6 * scale;
    }
}

void DrawGradientBackground(SDL_Renderer* renderer,
                            int width,
                            int height,
                            SDL_Color top,
                            SDL_Color bottom) {
    for (int y = 0; y < height; ++y) {
        const float t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        SDL_Color blended{
            static_cast<std::uint8_t>(top.r + ((bottom.r - top.r) * t)),
            static_cast<std::uint8_t>(top.g + ((bottom.g - top.g) * t)),
            static_cast<std::uint8_t>(top.b + ((bottom.b - top.b) * t)),
            255,
        };

        SetDrawColor(renderer, blended);
        SDL_RenderDrawLine(renderer, 0, y, width, y);
    }
}

std::string FormatDouble(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::uint8_t ScaleAlpha(std::uint8_t alpha, double opacity) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<double>(alpha) * opacity), 28L, 255L));
}

Palette ApplyOverlaySettings(const Palette& base, const framewatch::OverlaySettings& settings) {
    Palette adjusted = base;
    adjusted.panel.a = ScaleAlpha(base.panel.a, settings.panel_opacity);
    adjusted.panel_border.a = ScaleAlpha(base.panel_border.a, settings.panel_opacity);
    return adjusted;
}

void SetStatus(WindowState& state, std::string message, std::chrono::seconds ttl) {
    state.status_text = std::move(message);
    state.status_until = SteadyClock::now() + ttl;
}

std::string TargetLabel(const framewatch::DesktopWindowInfo& window) {
    const std::string owner = SanitizeUiText(window.owner_name, 14);
    const std::string title = SanitizeUiText(window.title, 18);
    if (owner.empty()) {
        return title;
    }
    if (title.empty()) {
        return owner;
    }
    return owner + "/" + title;
}

std::string TargetQueryForPersistence(const framewatch::DesktopWindowInfo& window) {
    if (!window.title.empty()) {
        return window.title;
    }
    return window.owner_name;
}

bool IsSelfWindow(const framewatch::DesktopWindowInfo& window, std::string_view marker) {
    return ContainsIgnoreCase(window.title, marker) || ContainsIgnoreCase(window.owner_name, marker);
}

void RefreshTargets(TargetingState& targeting, std::string_view self_marker) {
    if (!targeting.supported) {
        return;
    }

    const std::uint64_t previous_id =
        (targeting.selected_index >= 0 &&
         targeting.selected_index < static_cast<int>(targeting.windows.size()))
            ? targeting.windows[static_cast<std::size_t>(targeting.selected_index)].id
            : 0;

    std::vector<framewatch::DesktopWindowInfo> windows = framewatch::EnumerateDesktopWindows();
    windows.erase(std::remove_if(windows.begin(),
                                 windows.end(),
                                 [&](const framewatch::DesktopWindowInfo& window) {
                                     return IsSelfWindow(window, self_marker);
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
    int window_width = 0;
    int window_height = 0;
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

    const int preferred_y =
        align_bottom ? target.y + target.height - window_height : target.y;
    const int y = std::clamp(preferred_y,
                             usable_bounds.y,
                             usable_bounds.y + usable_bounds.h - window_height);

    SDL_SetWindowPosition(window, x, y);
}

void UpdateWindowTitle(SDL_Window* window,
                       const framewatch::MetricsSnapshot& live_metrics,
                       const framewatch::BenchmarkSummary& benchmark,
                       const WindowState& state,
                       const TargetingState& targeting) {
    std::ostringstream stream;
    stream << "FrameWatch Mini Debug Window";
    if (live_metrics.sample_count > 0) {
        stream << " | FPS " << std::fixed << std::setprecision(1) << live_metrics.current_fps;
    }
    stream << (state.running ? " | RUNNING" : " | PAUSED");

    if (benchmark.active) {
        stream << " | BENCH REC";
    } else if (benchmark.has_data) {
        stream << " | BENCH READY";
    }

    if (CurrentTarget(targeting).has_value()) {
        stream << " | TARGET LOCKED";
    }

    SDL_SetWindowTitle(window, stream.str().c_str());
}

void DrawCard(SDL_Renderer* renderer,
              const SDL_Rect& rect,
              SDL_Color panel_color,
              SDL_Color border_color,
              SDL_Color accent_color,
              SDL_Color label_color,
              SDL_Color value_color,
              std::string_view label,
              std::string_view value) {
    FillRect(renderer, rect, panel_color);
    DrawRect(renderer, rect, border_color);
    FillRect(renderer, SDL_Rect{rect.x, rect.y, rect.w, 4}, accent_color);
    DrawText(renderer, rect.x + 16, rect.y + 16, 2, label_color, label);
    DrawText(renderer, rect.x + 16, rect.y + 48, 3, value_color, value);
}

void DrawHeader(SDL_Renderer* renderer,
                int width,
                const Palette& palette,
                const WindowState& state,
                const framewatch::BenchmarkSummary& benchmark,
                const TargetingState& targeting) {
    const SDL_Rect rect{24, 24, width - 48, 88};
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer, rect.x + 20, rect.y + 18, 3, palette.text_primary, "FRAMEWATCH MINI");
    DrawText(renderer, rect.x + 22, rect.y + 50, 2, palette.text_muted, "DEBUG BENCH WINDOW");

    const SDL_Color state_color = state.running ? palette.accent : palette.warning;
    const SDL_Rect run_chip{rect.x + rect.w - 360, rect.y + 18, 104, 28};
    FillRect(renderer, run_chip, SDL_Color{state_color.r, state_color.g, state_color.b, 48});
    DrawRect(renderer, run_chip, state_color);
    DrawText(renderer,
             run_chip.x + 12,
             run_chip.y + 7,
             2,
             state_color,
             state.running ? "RUN" : "PAUSE");

    const SDL_Color bench_color = benchmark.active ? palette.danger : palette.accent_2;
    const SDL_Rect bench_chip{rect.x + rect.w - 240, rect.y + 18, 96, 28};
    FillRect(renderer, bench_chip, SDL_Color{bench_color.r, bench_color.g, bench_color.b, 48});
    DrawRect(renderer, bench_chip, bench_color);
    DrawText(renderer,
             bench_chip.x + 12,
             bench_chip.y + 7,
             2,
             bench_color,
             benchmark.active ? "REC" : (benchmark.has_data ? "READY" : "IDLE"));

    const SDL_Color target_color = CurrentTarget(targeting).has_value() ? palette.accent : palette.text_muted;
    const SDL_Rect target_chip{rect.x + rect.w - 128, rect.y + 18, 108, 28};
    FillRect(renderer, target_chip, SDL_Color{target_color.r, target_color.g, target_color.b, 40});
    DrawRect(renderer, target_chip, target_color);
    DrawText(renderer,
             target_chip.x + 12,
             target_chip.y + 7,
             2,
             target_color,
             CurrentTarget(targeting).has_value() ? "TARGET" : "NO TARGET");
}

void DrawStatsGrid(SDL_Renderer* renderer,
                   int width,
                   const Palette& palette,
                   const framewatch::MetricsSnapshot& live_metrics,
                   const framewatch::BenchmarkSummary& benchmark,
                   const TargetingState& targeting) {
    constexpr int columns = 4;
    constexpr int card_height = 104;
    constexpr int horizontal_gap = 16;
    constexpr int vertical_gap = 16;
    constexpr int top = 132;
    const int grid_width = width - 48;
    const int card_width = (grid_width - ((columns - 1) * horizontal_gap)) / columns;

    const std::array<std::pair<std::string, std::string>, 8> items{{
        {"FPS", FormatDouble(live_metrics.current_fps, 1)},
        {"AVERAGE", FormatDouble(live_metrics.average_fps, 1)},
        {"1% LOW", FormatDouble(live_metrics.one_percent_low_fps, 1)},
        {"0.1% LOW", FormatDouble(live_metrics.point_one_percent_low_fps, 1)},
        {"BENCH", benchmark.active ? "REC" : (benchmark.has_data ? "READY" : "IDLE")},
        {"BENCH TIME", FormatDouble(benchmark.duration_seconds, 1) + " S"},
        {"BENCH FRAMES", std::to_string(benchmark.frame_count)},
        {"TARGET", CurrentTarget(targeting).has_value() ? "LOCKED" : "NONE"},
    }};

    const std::array<SDL_Color, 8> accents{
        palette.accent,
        palette.accent_2,
        palette.warning,
        palette.danger,
        benchmark.active ? palette.danger : palette.accent_2,
        palette.accent,
        palette.accent_2,
        CurrentTarget(targeting).has_value() ? palette.accent : palette.text_muted,
    };

    for (std::size_t i = 0; i < items.size(); ++i) {
        const int row = static_cast<int>(i) / columns;
        const int column = static_cast<int>(i) % columns;
        const SDL_Rect rect{
            24 + column * (card_width + horizontal_gap),
            top + row * (card_height + vertical_gap),
            card_width,
            card_height,
        };

        DrawCard(renderer,
                 rect,
                 palette.panel,
                 palette.panel_border,
                 accents[i],
                 palette.text_muted,
                 palette.text_primary,
                 items[i].first,
                 items[i].second);
    }
}

void DrawGraph(SDL_Renderer* renderer,
               const SDL_Rect& rect,
               const Palette& palette,
               const framewatch::OverlaySnapshot& snapshot,
               std::string_view label) {
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer, rect.x + 18, rect.y + 16, 2, palette.text_primary, label);
    DrawText(renderer,
             rect.x + rect.w - 152,
             rect.y + 16,
             2,
             palette.text_muted,
             std::string("MS ") + FormatDouble(snapshot.graph_min_ms, 1) + " / " +
                 FormatDouble(snapshot.graph_max_ms, 1));

    const SDL_Rect plot_rect{rect.x + 18, rect.y + 52, rect.w - 36, rect.h - 78};
    FillRect(renderer, plot_rect, SDL_Color{10, 15, 25, 255});
    DrawRect(renderer, plot_rect, palette.grid);

    for (int i = 1; i < 5; ++i) {
        const int grid_y = plot_rect.y + (i * plot_rect.h) / 5;
        SetDrawColor(renderer, SDL_Color{palette.grid.r, palette.grid.g, palette.grid.b, 120});
        SDL_RenderDrawLine(renderer, plot_rect.x, grid_y, plot_rect.x + plot_rect.w, grid_y);
    }

    for (int i = 1; i < 7; ++i) {
        const int grid_x = plot_rect.x + (i * plot_rect.w) / 7;
        SetDrawColor(renderer, SDL_Color{palette.grid.r, palette.grid.g, palette.grid.b, 90});
        SDL_RenderDrawLine(renderer, grid_x, plot_rect.y, grid_x, plot_rect.y + plot_rect.h);
    }

    if (snapshot.graph.size() < 2) {
        DrawText(renderer,
                 plot_rect.x + 20,
                 plot_rect.y + plot_rect.h / 2 - 8,
                 2,
                 palette.text_muted,
                 "COLLECTING FRAME DATA");
        return;
    }

    const double spike_threshold = snapshot.graph_min_ms +
                                   ((snapshot.graph_max_ms - snapshot.graph_min_ms) * 0.75);

    for (std::size_t i = 1; i < snapshot.graph.size(); ++i) {
        const auto& previous = snapshot.graph[i - 1];
        const auto& current = snapshot.graph[i];

        const int x1 = plot_rect.x + static_cast<int>(std::lround(previous.x * plot_rect.w));
        const int y1 = plot_rect.y + plot_rect.h -
                       static_cast<int>(std::lround(previous.y * plot_rect.h));
        const int x2 = plot_rect.x + static_cast<int>(std::lround(current.x * plot_rect.w));
        const int y2 = plot_rect.y + plot_rect.h -
                       static_cast<int>(std::lround(current.y * plot_rect.h));

        const SDL_Color line_color =
            current.frametime_ms >= spike_threshold ? palette.warning : palette.accent_2;
        SetDrawColor(renderer, line_color);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    }

    const auto& latest = snapshot.graph.back();
    const int latest_x = plot_rect.x + static_cast<int>(std::lround(latest.x * plot_rect.w));
    const int latest_y = plot_rect.y + plot_rect.h -
                         static_cast<int>(std::lround(latest.y * plot_rect.h));
    FillRect(renderer, SDL_Rect{latest_x - 3, latest_y - 3, 7, 7}, palette.accent);
}

void DrawInfoPanel(SDL_Renderer* renderer,
                   const SDL_Rect& rect,
                   const Palette& palette,
                   SDL_Color accent,
                   std::string_view title,
                   const std::vector<std::string>& lines) {
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);
    FillRect(renderer, SDL_Rect{rect.x, rect.y, rect.w, 4}, accent);
    DrawText(renderer, rect.x + 16, rect.y + 16, 2, palette.text_primary, title);

    int y = rect.y + 48;
    for (const std::string& line : lines) {
        DrawText(renderer, rect.x + 16, y, 2, palette.text_muted, SanitizeUiText(line, 32));
        y += 20;
    }
}

void DrawSidebar(SDL_Renderer* renderer,
                 const SDL_Rect& rect,
                 const Palette& palette,
                 const framewatch::BenchmarkSummary& benchmark,
                 const TargetingState& targeting,
                 const framewatch::OverlaySettings& settings) {
    const int gap = 16;
    const int panel_height = (rect.h - gap) / 2;
    const SDL_Rect benchmark_rect{rect.x, rect.y, rect.w, panel_height};
    const SDL_Rect target_rect{rect.x, rect.y + panel_height + gap, rect.w, rect.h - panel_height - gap};

    std::vector<std::string> benchmark_lines{
        std::string("STATE ") + (benchmark.active ? "RECORDING" : (benchmark.has_data ? "READY" : "IDLE")),
        std::string("TIME ") + FormatDouble(benchmark.duration_seconds, 1) + " S",
        std::string("FRAMES ") + std::to_string(benchmark.frame_count),
        std::string("AVG ") + FormatDouble(benchmark.metrics.average_fps, 1),
        std::string("1% LOW ") + FormatDouble(benchmark.metrics.one_percent_low_fps, 1),
        "B START/STOP",
        "R RESET",
        "E EXPORT",
    };
    DrawInfoPanel(renderer, benchmark_rect, palette, benchmark.active ? palette.danger : palette.accent_2, "BENCHMARK", benchmark_lines);

    std::vector<std::string> target_lines;
    if (!targeting.supported) {
        target_lines = {"TARGETING UNSUPPORTED", "WINDOW PICKING NEEDS A PLATFORM BACKEND"};
    } else {
        const auto current = CurrentTarget(targeting);
        target_lines.push_back(std::string("FOLLOW ") + (targeting.follow_enabled ? "ON" : "OFF"));
        target_lines.push_back(std::string("DOCK ") +
                               std::string(framewatch::OverlayDockAnchorName(settings.dock_anchor)));
        target_lines.push_back(std::string("OPACITY ") +
                               std::to_string(static_cast<int>(std::lround(settings.panel_opacity * 100.0))) + "%");
        target_lines.push_back(std::string("QUERY ") +
                               (targeting.title_query.empty() ? "AUTO" : SanitizeUiText(targeting.title_query, 22)));
        target_lines.push_back(std::string("VISIBLE ") + std::to_string(targeting.windows.size()));
        if (current.has_value()) {
            target_lines.push_back(std::string("OWNER ") + SanitizeUiText(current->owner_name, 22));
            target_lines.push_back(std::string("TITLE ") + SanitizeUiText(current->title, 22));
            target_lines.push_back(std::string("BOUNDS ") + std::to_string(current->x) + "," +
                                   std::to_string(current->y) + " " +
                                   std::to_string(current->width) + "X" +
                                   std::to_string(current->height));
        } else {
            target_lines.push_back("NO TARGET SELECTED");
        }
        target_lines.push_back(std::string("GRAPH ") + (settings.show_graph ? "ON" : "OFF") +
                               "  SIDEBAR " + (settings.show_sidebar ? "ON" : "OFF"));
        target_lines.push_back("TAB NEXT  SHIFT+TAB PREV");
        target_lines.push_back("G FRONTMOST  F FOLLOW  N CLEAR");
        target_lines.push_back("C DOCK  [ ] OPACITY  V/I TOGGLE");
        target_lines.push_back("S PANEL  T QUERY  D RESET");
    }
    DrawInfoPanel(renderer, target_rect, palette, palette.accent, "TARGET WINDOW", target_lines);
}

bool PointInRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

int ComputeSettingsVisibleRows(int height) {
    const int max_panel_height = std::max(320, height - (kSettingsPanelMargin * 2));
    const int buttons_height = (kSettingsButtonHeight * 5) + (kSettingsButtonGap * 4);
    const int target_list_y = kSettingsButtonStartY + buttons_height + 12;
    const int reserved_height = target_list_y + kSettingsTargetListHeaderHeight + 12 +
                                kSettingsInfoHeight + kSettingsBottomPadding;
    return std::clamp((max_panel_height - reserved_height) / kSettingsTargetRowStride,
                      1,
                      kSettingsMaxVisibleRows);
}

int ClampTargetListStartIndex(const TargetingState& targeting,
                              int visible_rows,
                              int start_index) {
    if (targeting.windows.empty()) {
        return 0;
    }

    const int max_start =
        std::max(0, static_cast<int>(targeting.windows.size()) - visible_rows);
    return std::clamp(start_index, 0, max_start);
}

void DrawSettingsButton(SDL_Renderer* renderer,
                        const Palette& palette,
                        const SettingsPanelButton& button) {
    const SDL_Color accent = button.accent ? palette.accent : palette.accent_2;
    FillRect(renderer, button.rect, SDL_Color{18, 24, 34, 240});
    DrawRect(renderer, button.rect, accent);

    const int label_width = TextWidth(button.label, 1);
    const int text_x = button.rect.x + std::max(12, (button.rect.w - label_width) / 2);
    const int text_y = button.rect.y + std::max(8, (button.rect.h - 8) / 2);
    DrawText(renderer, text_x, text_y, 1, palette.text_primary, button.label);
}

SettingsPanelLayout BuildSettingsPanelLayout(int width,
                                             int height,
                                             const framewatch::OverlaySettings& settings,
                                             const TargetingState& targeting,
                                             int target_list_start_index) {
    SettingsPanelLayout layout;
    const int panel_width = std::min(width - 72, 520);
    layout.visible_rows = ComputeSettingsVisibleRows(height);
    const int buttons_height = (kSettingsButtonHeight * 5) + (kSettingsButtonGap * 4);
    const int target_list_y = kSettingsButtonStartY + buttons_height + 12;
    const int reserved_height = target_list_y + kSettingsTargetListHeaderHeight + 12 +
                                kSettingsInfoHeight + kSettingsBottomPadding;
    const int target_list_height =
        kSettingsTargetListHeaderHeight + (layout.visible_rows * kSettingsTargetRowStride);
    const int panel_height = reserved_height + (layout.visible_rows * kSettingsTargetRowStride);
    const int panel_y = std::max(kSettingsPanelMargin, (height - panel_height) / 2);

    layout.panel_rect = SDL_Rect{(width - panel_width) / 2, panel_y, panel_width, panel_height};
    const int query_actions_width = 72;
    const int query_actions_gap = 8;
    const int query_row_width = panel_width - 36;
    const int query_input_width =
        query_row_width - (query_actions_width * 2) - (query_actions_gap * 2);
    layout.query_rect =
        SDL_Rect{layout.panel_rect.x + 18, layout.panel_rect.y + 46, query_input_width, 34};
    layout.query_apply_rect = SDL_Rect{layout.query_rect.x + layout.query_rect.w + query_actions_gap,
                                       layout.query_rect.y,
                                       query_actions_width,
                                       34};
    layout.query_clear_rect =
        SDL_Rect{layout.query_apply_rect.x + query_actions_width + query_actions_gap,
                 layout.query_rect.y,
                 query_actions_width,
                 34};

    const int button_width = (panel_width - 54) / 2;
    const int button_origin_y = layout.panel_rect.y + kSettingsButtonStartY;

    auto add_button = [&](int row,
                          int column,
                          SettingsPanelAction action,
                          std::string label,
                          bool accent = false) {
        layout.buttons.push_back(SettingsPanelButton{
            SDL_Rect{layout.panel_rect.x + 18 + column * (button_width + kSettingsButtonGap),
                     button_origin_y + row * (kSettingsButtonHeight + kSettingsButtonGap),
                     button_width,
                     kSettingsButtonHeight},
            action,
            std::move(label),
            accent,
        });
    };

    add_button(0,
               0,
               SettingsPanelAction::ToggleGraph,
               std::string("GRAPH ") + (settings.show_graph ? "ON" : "OFF"),
               settings.show_graph);
    add_button(0,
               1,
               SettingsPanelAction::ToggleSidebar,
               std::string("SIDEBAR ") + (settings.show_sidebar ? "ON" : "OFF"),
               settings.show_sidebar);
    add_button(1,
               0,
               SettingsPanelAction::ToggleFollow,
               std::string("FOLLOW ") + (targeting.follow_enabled ? "ON" : "OFF"),
               targeting.follow_enabled);
    add_button(1,
               1,
               SettingsPanelAction::CycleDock,
               std::string("DOCK ") + std::string(framewatch::OverlayDockAnchorName(settings.dock_anchor)));
    add_button(2, 0, SettingsPanelAction::OpacityDown, "OPACITY -");
    add_button(2,
               1,
               SettingsPanelAction::OpacityUp,
               std::string("OPACITY +"));
    add_button(3, 0, SettingsPanelAction::TargetNext, "TARGET NEXT");
    add_button(3, 1, SettingsPanelAction::TargetFront, "TARGET FRONT");
    add_button(4, 0, SettingsPanelAction::ClearTarget, "CLEAR TARGET");
    add_button(4, 1, SettingsPanelAction::ResetDefaults, "RESET DEFAULTS");

    layout.target_list_rect = SDL_Rect{
        layout.panel_rect.x + 18,
        layout.panel_rect.y + target_list_y,
        panel_width - 36,
        target_list_height,
    };
    layout.target_page_prev_rect = SDL_Rect{
        layout.target_list_rect.x + layout.target_list_rect.w - 54,
        layout.target_list_rect.y + 4,
        20,
        14,
    };
    layout.target_page_next_rect = SDL_Rect{
        layout.target_list_rect.x + layout.target_list_rect.w - 28,
        layout.target_list_rect.y + 4,
        20,
        14,
    };

    if (!targeting.windows.empty()) {
        layout.first_visible_index =
            ClampTargetListStartIndex(targeting, layout.visible_rows, target_list_start_index);
        const int max_start =
            std::max(0, static_cast<int>(targeting.windows.size()) - layout.visible_rows);
        layout.has_prev_page = layout.first_visible_index > 0;
        layout.has_next_page = layout.first_visible_index < max_start;

        for (int row = 0; row < layout.visible_rows; ++row) {
            const int window_index = layout.first_visible_index + row;
            if (window_index >= static_cast<int>(targeting.windows.size())) {
                break;
            }

            layout.target_rows.push_back(SettingsPanelTargetRow{
                SDL_Rect{
                    layout.target_list_rect.x + 8,
                    layout.target_list_rect.y + kSettingsTargetListHeaderHeight + 2 +
                        row * kSettingsTargetRowStride,
                    layout.target_list_rect.w - 16,
                    kSettingsTargetRowStride - 4,
                },
                window_index,
            });
        }
    } else {
        layout.first_visible_index = 0;
        layout.has_prev_page = false;
        layout.has_next_page = false;
    }

    return layout;
}

void DrawSettingsOverlay(SDL_Renderer* renderer,
                         int width,
                         int height,
                         const Palette& palette,
                         const framewatch::OverlaySettings& settings,
                         const TargetingState& targeting,
                         const WindowState& state,
                         const std::filesystem::path& settings_path,
                         int window_width,
                         int window_height,
                         int window_x,
                         int window_y) {
    FillRect(renderer, SDL_Rect{0, 0, width, height}, SDL_Color{4, 8, 14, 170});

    const SettingsPanelLayout layout =
        BuildSettingsPanelLayout(width, height, settings, targeting, state.target_list_start_index);
    FillRect(renderer, layout.panel_rect, palette.panel);
    DrawRect(renderer, layout.panel_rect, palette.panel_border);
    FillRect(renderer,
             SDL_Rect{layout.panel_rect.x, layout.panel_rect.y, layout.panel_rect.w, 4},
             palette.warning);

    DrawText(renderer,
             layout.panel_rect.x + 16,
             layout.panel_rect.y + 14,
             2,
             palette.text_primary,
             "SETTINGS");
    DrawText(renderer,
             layout.panel_rect.x + layout.panel_rect.w - 154,
             layout.panel_rect.y + 16,
             1,
             palette.text_muted,
             state.editing_target_query ? "ENTER APPLY  ESC CANCEL" : "QUERY / APPLY / CLEAR");

    FillRect(renderer,
             layout.query_rect,
             state.editing_target_query ? SDL_Color{22, 33, 49, 255} : SDL_Color{14, 20, 28, 255});
    DrawRect(renderer, layout.query_rect, state.editing_target_query ? palette.accent : palette.grid);
    DrawText(renderer,
             layout.query_rect.x + 10,
             layout.query_rect.y + 9,
             1,
             palette.text_primary,
             state.editing_target_query
                 ? SanitizeUiText(state.target_query_buffer + "_", 46)
                 : std::string("QUERY ") +
                       (targeting.title_query.empty() ? "NONE"
                                                      : SanitizeUiText(targeting.title_query, 38)));
    DrawSettingsButton(renderer,
                       palette,
                       SettingsPanelButton{layout.query_apply_rect,
                                           SettingsPanelAction::ToggleGraph,
                                           "APPLY",
                                           state.editing_target_query ||
                                               !targeting.title_query.empty()});
    DrawSettingsButton(renderer,
                       palette,
                       SettingsPanelButton{layout.query_clear_rect,
                                           SettingsPanelAction::ClearTarget,
                                           "CLEAR",
                                           state.editing_target_query ||
                                               !targeting.title_query.empty()});

    for (const SettingsPanelButton& button : layout.buttons) {
        DrawSettingsButton(renderer, palette, button);
    }

    FillRect(renderer, layout.target_list_rect, SDL_Color{12, 18, 26, 255});
    DrawRect(renderer, layout.target_list_rect, palette.grid);
    DrawText(renderer,
             layout.target_list_rect.x + 10,
             layout.target_list_rect.y + 7,
             1,
             palette.text_muted,
             "VISIBLE WINDOWS");
    FillRect(renderer,
             layout.target_page_prev_rect,
             layout.has_prev_page ? SDL_Color{18, 28, 40, 255} : SDL_Color{14, 20, 28, 180});
    DrawRect(renderer,
             layout.target_page_prev_rect,
             layout.has_prev_page ? palette.accent_2 : palette.grid);
    DrawText(renderer,
             layout.target_page_prev_rect.x + 7,
             layout.target_page_prev_rect.y + 4,
             1,
             layout.has_prev_page ? palette.text_primary : palette.text_muted,
             "<");
    FillRect(renderer,
             layout.target_page_next_rect,
             layout.has_next_page ? SDL_Color{18, 28, 40, 255} : SDL_Color{14, 20, 28, 180});
    DrawRect(renderer,
             layout.target_page_next_rect,
             layout.has_next_page ? palette.accent_2 : palette.grid);
    DrawText(renderer,
             layout.target_page_next_rect.x + 7,
             layout.target_page_next_rect.y + 4,
             1,
             layout.has_next_page ? palette.text_primary : palette.text_muted,
             ">");
    const std::string rows_label =
        layout.target_rows.empty()
            ? std::string("ROWS 0/0")
            : std::string("ROWS ") + std::to_string(layout.first_visible_index + 1) + "-" +
                  std::to_string(layout.first_visible_index + layout.target_rows.size()) + "/" +
                  std::to_string(targeting.windows.size());
    DrawText(renderer,
             layout.target_list_rect.x + layout.target_list_rect.w - 196,
             layout.target_list_rect.y + 7,
             1,
             palette.text_muted,
             rows_label);

    if (layout.target_rows.empty()) {
        DrawText(renderer,
                 layout.target_list_rect.x + 10,
                 layout.target_list_rect.y + 30,
                 1,
                 palette.text_muted,
                 "NO TARGETABLE WINDOWS");
    } else {
        for (const SettingsPanelTargetRow& row : layout.target_rows) {
            const bool selected = row.window_index == targeting.selected_index;
            const SDL_Color row_border = selected ? palette.accent : palette.grid;
            const SDL_Color row_fill = selected ? SDL_Color{20, 52, 48, 255}
                                                : SDL_Color{16, 23, 33, 255};
            FillRect(renderer, row.rect, row_fill);
            DrawRect(renderer, row.rect, row_border);

            const auto& window = targeting.windows[static_cast<std::size_t>(row.window_index)];
            const std::string row_label =
                std::to_string(row.window_index + 1) + ". " + TargetLabel(window) + "  " +
                std::to_string(window.width) + "x" + std::to_string(window.height);
            DrawText(renderer,
                     row.rect.x + 8,
                     row.rect.y + 6,
                     1,
                     selected ? palette.text_primary : palette.text_muted,
                     SanitizeUiText(row_label, 54));
        }
    }

    const int info_y = layout.target_list_rect.y + layout.target_list_rect.h + 16;
    DrawText(renderer,
             layout.panel_rect.x + 18,
             info_y,
             1,
             palette.text_muted,
             std::string("WINDOW ") + std::to_string(window_width) + "X" + std::to_string(window_height) +
                 "  POS " + std::to_string(window_x) + "," + std::to_string(window_y));
    DrawText(renderer,
             layout.panel_rect.x + 18,
             info_y + 18,
             1,
             palette.text_muted,
             std::string("VISIBLE TARGETS ") + std::to_string(targeting.windows.size()) +
                 "  WHEEL/PAGE TO SCROLL");
    DrawText(renderer,
             layout.panel_rect.x + 18,
             info_y + 36,
             1,
             palette.text_muted,
             std::string("FILE ") + SanitizeUiText(settings_path.string(), 52));
}

void DrawFooter(SDL_Renderer* renderer,
                int width,
                int height,
                const Palette& palette,
                const WindowState& state) {
    const SDL_Rect rect{24, height - 70, width - 48, 46};
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer,
             rect.x + 16,
             rect.y + 6,
             1,
             palette.text_muted,
             "SPACE PAUSE  B BENCH  R RESET  E EXPORT  S SETTINGS  T QUERY  TAB CYCLE\nG FRONT  F FOLLOW  N CLEAR  C DOCK  [ ] OPACITY  V GRAPH  I SIDE  PG/WHEEL LIST  ESC QUIT");

    const SDL_Color status_color =
        (state.status_until > SteadyClock::now()) ? palette.accent : palette.text_muted;
    const std::string status = SanitizeUiText(state.status_text, 28);
    DrawText(renderer,
             rect.x + rect.w - TextWidth(status, 2) - 16,
             rect.y + 14,
             2,
             status_color,
             status);
}

bool ExportSession(const framewatch::PerformanceSession& benchmark, const AppOptions& options) {
    const bool ok = benchmark.ExportPreferred(options.csv_path, options.json_path);
    std::cout << "Export CSV/JSON -> " << (ok ? "ok" : "failed") << '\n';
    std::cout << "CSV: " << options.csv_path.string() << '\n';
    std::cout << "JSON: " << options.json_path.string() << '\n';
    return ok;
}

int ListTargets() {
    if (!framewatch::WindowTargetingSupported()) {
        std::cout << "Window targeting is not supported on this platform\n";
        return EXIT_SUCCESS;
    }

    const auto windows = framewatch::EnumerateDesktopWindows();
    if (windows.empty()) {
        std::cout << "No targetable desktop windows were found\n";
        return EXIT_SUCCESS;
    }

    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& window = windows[i];
        std::cout << i << ": [" << window.owner_name << "] " << window.title << " @ "
                  << window.x << "," << window.y << " " << window.width << "x" << window.height
                  << '\n';
    }

    return EXIT_SUCCESS;
}

int RunSmokeTest(const AppOptions& options) {
    framewatch::PerformanceSession benchmark;
    benchmark.ResetSyntheticTimeline();
    SyntheticFrameGenerator generator;
    for (int i = 0; i < 180; ++i) {
        benchmark.CaptureSyntheticFrame(generator.NextFrametimeMs());
    }
    benchmark.StartBenchmark();
    for (int i = 0; i < 120; ++i) {
        benchmark.CaptureSyntheticFrame(generator.NextFrametimeMs());
    }
    benchmark.StopBenchmark();

    const auto live_metrics = benchmark.LiveMetrics();
    const auto benchmark_summary = benchmark.CurrentBenchmark();
    const auto overlay = benchmark.GraphSnapshot();
    const bool exported = ExportSession(benchmark, options);

    std::cout << "Live FPS: " << FormatDouble(live_metrics.current_fps, 2) << '\n';
    std::cout << "Benchmark frames: " << benchmark_summary.frame_count << '\n';
    std::cout << "Benchmark time: " << FormatDouble(benchmark_summary.duration_seconds, 2) << '\n';
    std::cout << "Overlay points: " << overlay.graph.size() << '\n';

    if (!options.target_title.empty()) {
        const auto target = framewatch::FindDesktopWindowByTitle(options.target_title);
        std::cout << "Target query: " << options.target_title << " -> "
                  << (target.has_value() ? TargetLabel(*target) : "not found") << '\n';
    }

    return exported ? EXIT_SUCCESS : EXIT_FAILURE;
}

int RunWindow(const AppOptions& options) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    framewatch::OverlaySettings overlay_settings;
    if (const auto loaded_settings = framewatch::LoadOverlaySettings(options.settings_path)) {
        overlay_settings = *loaded_settings;
    }

    const int initial_window_x =
        overlay_settings.window_x.value_or(SDL_WINDOWPOS_CENTERED);
    const int initial_window_y =
        overlay_settings.window_y.value_or(SDL_WINDOWPOS_CENTERED);
    SDL_Window* window = SDL_CreateWindow(kSelfTitleMarker.data(),
                                          initial_window_x,
                                          initial_window_y,
                                          overlay_settings.window_width,
                                          overlay_settings.window_height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (renderer == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Palette palette;
    framewatch::PerformanceSession benchmark;
    benchmark.ResetSyntheticTimeline();
    SyntheticFrameGenerator generator;
    WindowState state;
    TargetingState targeting;
    targeting.follow_enabled =
        options.follow_target || overlay_settings.follow_target_window;
    targeting.title_query = !options.target_title.empty() ? options.target_title
                                                          : overlay_settings.target_window_query;

    RefreshTargets(targeting, kSelfTitleMarker);
    if (!targeting.title_query.empty() && !CurrentTarget(targeting).has_value()) {
        SetStatus(state, "TARGET QUERY NOT FOUND", std::chrono::seconds(3));
    } else if (targeting.follow_enabled && !CurrentTarget(targeting).has_value()) {
        PickFrontmostTarget(targeting, kSelfTitleMarker);
        if (const auto target = CurrentTarget(targeting)) {
            targeting.title_query = TargetQueryForPersistence(*target);
        }
    }

    auto settings_visible_rows = [&]() {
        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        return ComputeSettingsVisibleRows(height);
    };

    auto clamp_target_list_start = [&]() {
        state.target_list_start_index =
            ClampTargetListStartIndex(targeting,
                                      settings_visible_rows(),
                                      state.target_list_start_index);
    };

    auto ensure_selected_target_visible = [&]() {
        const int visible_rows = settings_visible_rows();
        if (targeting.selected_index >= 0) {
            if (targeting.selected_index < state.target_list_start_index) {
                state.target_list_start_index = targeting.selected_index;
            } else if (targeting.selected_index >=
                       (state.target_list_start_index + visible_rows)) {
                state.target_list_start_index =
                    targeting.selected_index - visible_rows + 1;
            }
        }

        state.target_list_start_index =
            ClampTargetListStartIndex(targeting, visible_rows, state.target_list_start_index);
    };

    auto scroll_target_list = [&](int delta_rows) {
        if (targeting.windows.empty() || delta_rows == 0) {
            return;
        }

        state.target_list_start_index =
            ClampTargetListStartIndex(targeting,
                                      settings_visible_rows(),
                                      state.target_list_start_index + delta_rows);
    };

    auto page_target_list = [&](int direction) {
        const int visible_rows = settings_visible_rows();
        scroll_target_list(direction * std::max(1, visible_rows - 1));
    };

    ensure_selected_target_visible();

    auto persist_overlay_settings = [&]() {
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSize(window, &window_width, &window_height);
        overlay_settings.window_width = std::max(window_width, 640);
        overlay_settings.window_height = std::max(window_height, 420);

        int window_x = 0;
        int window_y = 0;
        SDL_GetWindowPosition(window, &window_x, &window_y);
        overlay_settings.window_x = window_x;
        overlay_settings.window_y = window_y;

        overlay_settings.follow_target_window = targeting.follow_enabled;
        overlay_settings.target_window_query = targeting.title_query;
        if (!framewatch::SaveOverlaySettings(overlay_settings, options.settings_path)) {
            SetStatus(state, "SETTINGS SAVE FAILED", std::chrono::seconds(3));
        }
    };

    auto stop_target_query_edit = [&]() {
        if (!state.editing_target_query) {
            return;
        }
        state.editing_target_query = false;
        state.target_query_buffer.clear();
        SDL_StopTextInput();
    };

    auto begin_target_query_edit = [&]() {
        state.show_settings_panel = true;
        state.editing_target_query = true;
        state.target_query_buffer = targeting.title_query;
        SDL_StartTextInput();
        SetStatus(state, "EDIT TARGET QUERY", std::chrono::seconds(2));
    };

    auto apply_target_query = [&](std::string query, bool stop_edit_mode) {
        targeting.title_query = TrimWhitespace(query);
        targeting.selected_index = -1;
        RefreshTargets(targeting, kSelfTitleMarker);
        if (stop_edit_mode) {
            stop_target_query_edit();
        }
        ensure_selected_target_visible();

        if (targeting.title_query.empty()) {
            SetStatus(state, "TARGET QUERY CLEARED", std::chrono::seconds(2));
        } else if (CurrentTarget(targeting).has_value()) {
            SetStatus(state, "TARGET QUERY APPLIED", std::chrono::seconds(2));
        } else {
            SetStatus(state, "TARGET QUERY NO MATCH", std::chrono::seconds(2));
        }

        persist_overlay_settings();
    };

    auto apply_target_query_edit = [&]() {
        apply_target_query(state.target_query_buffer, true);
    };

    auto apply_active_target_query = [&]() {
        apply_target_query(targeting.title_query, false);
    };

    auto cancel_target_query_edit = [&]() {
        stop_target_query_edit();
        SetStatus(state, "TARGET QUERY CANCELED", std::chrono::seconds(2));
    };

    auto clear_target_query = [&]() {
        stop_target_query_edit();
        targeting.title_query.clear();
        targeting.selected_index = -1;
        state.target_list_start_index = 0;
        RefreshTargets(targeting, kSelfTitleMarker);
        SetStatus(state, "TARGET QUERY CLEARED", std::chrono::seconds(2));
        persist_overlay_settings();
    };

    auto select_target_row = [&](int window_index) {
        if (window_index < 0 || window_index >= static_cast<int>(targeting.windows.size())) {
            return;
        }

        targeting.selected_index = window_index;
        targeting.title_query =
            TargetQueryForPersistence(targeting.windows[static_cast<std::size_t>(window_index)]);
        ensure_selected_target_visible();
        SetStatus(state, "TARGET SELECTED", std::chrono::seconds(2));
        persist_overlay_settings();
    };

    auto perform_settings_action = [&](SettingsPanelAction action) {
        switch (action) {
            case SettingsPanelAction::ToggleGraph:
                overlay_settings.show_graph = !overlay_settings.show_graph;
                SetStatus(state,
                          overlay_settings.show_graph ? "GRAPH VISIBLE" : "GRAPH HIDDEN",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::ToggleSidebar:
                overlay_settings.show_sidebar = !overlay_settings.show_sidebar;
                SetStatus(state,
                          overlay_settings.show_sidebar ? "SIDEBAR VISIBLE"
                                                        : "SIDEBAR HIDDEN",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::ToggleFollow:
                targeting.follow_enabled = !targeting.follow_enabled;
                SetStatus(state,
                          targeting.follow_enabled ? "FOLLOW TARGET ON" : "FOLLOW TARGET OFF",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::CycleDock:
                overlay_settings.dock_anchor =
                    framewatch::CycleOverlayDockAnchor(overlay_settings.dock_anchor);
                SetStatus(state,
                          std::string("DOCK ") +
                              std::string(framewatch::OverlayDockAnchorName(
                                  overlay_settings.dock_anchor)),
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::OpacityDown:
                framewatch::AdjustOverlayOpacity(overlay_settings, -0.10);
                SetStatus(state,
                          std::string("OPACITY ") +
                              std::to_string(static_cast<int>(std::lround(
                                  overlay_settings.panel_opacity * 100.0))) +
                              "%",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::OpacityUp:
                framewatch::AdjustOverlayOpacity(overlay_settings, 0.10);
                SetStatus(state,
                          std::string("OPACITY ") +
                              std::to_string(static_cast<int>(std::lround(
                                  overlay_settings.panel_opacity * 100.0))) +
                              "%",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::TargetNext:
                RefreshTargets(targeting, kSelfTitleMarker);
                CycleTarget(targeting, 1);
                if (const auto target = CurrentTarget(targeting)) {
                    targeting.title_query = TargetQueryForPersistence(*target);
                }
                ensure_selected_target_visible();
                SetStatus(state,
                          CurrentTarget(targeting).has_value() ? "TARGET CHANGED"
                                                               : "NO TARGET AVAILABLE",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::TargetFront:
                PickFrontmostTarget(targeting, kSelfTitleMarker);
                if (const auto target = CurrentTarget(targeting)) {
                    targeting.title_query = TargetQueryForPersistence(*target);
                }
                ensure_selected_target_visible();
                SetStatus(state,
                          CurrentTarget(targeting).has_value() ? "FRONTMOST TARGET LOCKED"
                                                               : "NO TARGET FOUND",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::ClearTarget:
                clear_target_query();
                break;
            case SettingsPanelAction::ResetDefaults:
                overlay_settings = framewatch::OverlaySettings{};
                targeting.follow_enabled = overlay_settings.follow_target_window;
                targeting.title_query = overlay_settings.target_window_query;
                targeting.selected_index = -1;
                state.target_list_start_index = 0;
                SDL_SetWindowSize(window,
                                  overlay_settings.window_width,
                                  overlay_settings.window_height);
                SDL_SetWindowPosition(window,
                                      SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED);
                SetStatus(state, "SETTINGS RESET", std::chrono::seconds(2));
                persist_overlay_settings();
                break;
        }
    };

    while (!state.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                state.quit = true;
            } else if (event.type == SDL_TEXTINPUT && state.editing_target_query) {
                for (const unsigned char ch : std::string_view(event.text.text)) {
                    if (ch < 32 || ch > 126 || state.target_query_buffer.size() >= 46) {
                        continue;
                    }
                    state.target_query_buffer.push_back(static_cast<char>(ch));
                }
            } else if (event.type == SDL_MOUSEWHEEL && state.show_settings_panel &&
                       !state.editing_target_query) {
                int width = 0;
                int height = 0;
                int mouse_x = 0;
                int mouse_y = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                SDL_GetMouseState(&mouse_x, &mouse_y);
                const SettingsPanelLayout layout =
                    BuildSettingsPanelLayout(width,
                                             height,
                                             overlay_settings,
                                             targeting,
                                             state.target_list_start_index);
                if (PointInRect(mouse_x, mouse_y, layout.target_list_rect)) {
                    scroll_target_list(-event.wheel.y);
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
                       state.show_settings_panel) {
                int width = 0;
                int height = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                const SettingsPanelLayout layout =
                    BuildSettingsPanelLayout(width,
                                             height,
                                             overlay_settings,
                                             targeting,
                                             state.target_list_start_index);
                const int mouse_x = event.button.x;
                const int mouse_y = event.button.y;

                if (!PointInRect(mouse_x, mouse_y, layout.panel_rect)) {
                    if (state.editing_target_query) {
                        cancel_target_query_edit();
                    }
                    state.show_settings_panel = false;
                    SetStatus(state, "SETTINGS PANEL CLOSED", std::chrono::seconds(2));
                } else if (PointInRect(mouse_x, mouse_y, layout.query_rect)) {
                    begin_target_query_edit();
                } else if (PointInRect(mouse_x, mouse_y, layout.query_apply_rect)) {
                    if (state.editing_target_query) {
                        apply_target_query_edit();
                    } else {
                        apply_active_target_query();
                    }
                } else if (PointInRect(mouse_x, mouse_y, layout.query_clear_rect)) {
                    clear_target_query();
                } else if (!state.editing_target_query) {
                    bool handled = false;
                    if (layout.has_prev_page &&
                        PointInRect(mouse_x, mouse_y, layout.target_page_prev_rect)) {
                        page_target_list(-1);
                        handled = true;
                    } else if (layout.has_next_page &&
                               PointInRect(mouse_x, mouse_y, layout.target_page_next_rect)) {
                        page_target_list(1);
                        handled = true;
                    }

                    if (handled) {
                        continue;
                    }

                    for (const SettingsPanelTargetRow& row : layout.target_rows) {
                        if (PointInRect(mouse_x, mouse_y, row.rect)) {
                            select_target_row(row.window_index);
                            handled = true;
                            break;
                        }
                    }

                    if (handled) {
                        continue;
                    }

                    for (const SettingsPanelButton& button : layout.buttons) {
                        if (PointInRect(mouse_x, mouse_y, button.rect)) {
                            perform_settings_action(button.action);
                            break;
                        }
                    }
                }
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                const bool shift_held = (event.key.keysym.mod & KMOD_SHIFT) != 0;

                if (state.editing_target_query) {
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            cancel_target_query_edit();
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            apply_target_query_edit();
                            break;
                        case SDLK_BACKSPACE:
                            if (!state.target_query_buffer.empty()) {
                                state.target_query_buffer.pop_back();
                            }
                            break;
                        default:
                            break;
                    }
                    continue;
                }

                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        if (state.show_settings_panel) {
                            state.show_settings_panel = false;
                            SetStatus(state, "SETTINGS PANEL CLOSED", std::chrono::seconds(2));
                        } else {
                            state.quit = true;
                        }
                        break;
                    case SDLK_s:
                        state.show_settings_panel = !state.show_settings_panel;
                        SetStatus(state,
                                  state.show_settings_panel ? "SETTINGS PANEL OPEN"
                                                            : "SETTINGS PANEL CLOSED",
                                  std::chrono::seconds(2));
                        break;
                    case SDLK_t:
                        begin_target_query_edit();
                        break;
                    case SDLK_PAGEUP:
                        if (state.show_settings_panel) {
                            page_target_list(-1);
                        }
                        break;
                    case SDLK_PAGEDOWN:
                        if (state.show_settings_panel) {
                            page_target_list(1);
                        }
                        break;
                    case SDLK_SPACE:
                        state.running = !state.running;
                        SetStatus(state,
                                  state.running ? "SIMULATION RUNNING" : "SIMULATION PAUSED",
                                  std::chrono::seconds(2));
                        break;
                    case SDLK_b:
                        benchmark.ToggleBenchmark();
                        SetStatus(state,
                                  benchmark.IsBenchmarkRecording() ? "BENCHMARK RECORDING"
                                                                   : "BENCHMARK STOPPED",
                                  std::chrono::seconds(2));
                        break;
                    case SDLK_r:
                        benchmark.Reset();
                        benchmark.ResetSyntheticTimeline();
                        generator.Reset();
                        SetStatus(state, "SESSION RESET", std::chrono::seconds(2));
                        break;
                    case SDLK_e:
                        SetStatus(state,
                                  ExportSession(benchmark, options) ? "EXPORT COMPLETE"
                                                                    : "EXPORT FAILED",
                                  std::chrono::seconds(3));
                        break;
                    case SDLK_TAB:
                        RefreshTargets(targeting, kSelfTitleMarker);
                        CycleTarget(targeting, shift_held ? -1 : 1);
                        if (const auto target = CurrentTarget(targeting)) {
                            targeting.title_query = TargetQueryForPersistence(*target);
                        }
                        ensure_selected_target_visible();
                        SetStatus(state,
                                  CurrentTarget(targeting).has_value() ? "TARGET CHANGED"
                                                                       : "NO TARGET AVAILABLE",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_g:
                        perform_settings_action(SettingsPanelAction::TargetFront);
                        break;
                    case SDLK_f:
                        perform_settings_action(SettingsPanelAction::ToggleFollow);
                        break;
                    case SDLK_n:
                        perform_settings_action(SettingsPanelAction::ClearTarget);
                        break;
                    case SDLK_c:
                        perform_settings_action(SettingsPanelAction::CycleDock);
                        break;
                    case SDLK_LEFTBRACKET:
                        perform_settings_action(SettingsPanelAction::OpacityDown);
                        break;
                    case SDLK_RIGHTBRACKET:
                        perform_settings_action(SettingsPanelAction::OpacityUp);
                        break;
                    case SDLK_v:
                        perform_settings_action(SettingsPanelAction::ToggleGraph);
                        break;
                    case SDLK_i:
                        perform_settings_action(SettingsPanelAction::ToggleSidebar);
                        break;
                    case SDLK_d:
                        perform_settings_action(SettingsPanelAction::ResetDefaults);
                        break;
                    default:
                        break;
                }
            }
        }

        const SteadyClock::time_point now = SteadyClock::now();
        if (state.running && (now - state.last_step_at) >= std::chrono::milliseconds(16)) {
            benchmark.CaptureSyntheticFrame(generator.NextFrametimeMs());
            state.last_step_at = now;
        }

        if ((now - state.last_target_refresh_at) >= std::chrono::milliseconds(750)) {
            RefreshTargets(targeting, kSelfTitleMarker);
            clamp_target_list_start();
            state.last_target_refresh_at = now;
        }

        if (targeting.follow_enabled &&
            (now - state.last_follow_sync_at) >= std::chrono::milliseconds(500)) {
            if (const auto target = CurrentTarget(targeting)) {
                DockWindowToTarget(window, *target, overlay_settings);
            }
            state.last_follow_sync_at = now;
        }

        const auto live_metrics = benchmark.LiveMetrics();
        const auto benchmark_summary = benchmark.CurrentBenchmark();

        if ((now - state.last_title_update_at) >= std::chrono::milliseconds(250)) {
            UpdateWindowTitle(window, live_metrics, benchmark_summary, state, targeting);
            state.last_title_update_at = now;
        }

        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        clamp_target_list_start();
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSize(window, &window_width, &window_height);
        int window_x = 0;
        int window_y = 0;
        SDL_GetWindowPosition(window, &window_x, &window_y);

        const Palette runtime_palette = ApplyOverlaySettings(palette, overlay_settings);
        const auto graph_snapshot = benchmark.GraphSnapshot();
        const int sidebar_width = overlay_settings.show_sidebar ? 272 : 0;
        const int graph_width =
            overlay_settings.show_sidebar ? width - 344 : width - 48;
        const SDL_Rect graph_rect{24, 364, std::max(320, graph_width), std::max(180, height - 458)};
        const SDL_Rect sidebar_rect{width - 296, 364, sidebar_width, std::max(180, height - 458)};

        DrawGradientBackground(renderer,
                               width,
                               height,
                               runtime_palette.background_top,
                               runtime_palette.background_bottom);
        DrawHeader(renderer, width, runtime_palette, state, benchmark_summary, targeting);
        DrawStatsGrid(renderer, width, runtime_palette, live_metrics, benchmark_summary, targeting);
        if (overlay_settings.show_graph) {
            DrawGraph(renderer, graph_rect, runtime_palette, graph_snapshot, benchmark.GraphLabel());
        } else {
            const std::vector<std::string> overlay_lines{
                "GRAPH HIDDEN",
                std::string("DOCK ") +
                    std::string(framewatch::OverlayDockAnchorName(overlay_settings.dock_anchor)),
                std::string("OPACITY ") +
                    std::to_string(static_cast<int>(std::lround(
                        overlay_settings.panel_opacity * 100.0))) +
                    "%",
                std::string("SIDEBAR ") + (overlay_settings.show_sidebar ? "ON" : "OFF"),
                "PRESS V TO SHOW GRAPH",
            };
            DrawInfoPanel(renderer,
                          graph_rect,
                          runtime_palette,
                          runtime_palette.accent_2,
                          "OVERLAY SETTINGS",
                          overlay_lines);
        }
        if (overlay_settings.show_sidebar) {
            DrawSidebar(renderer,
                        sidebar_rect,
                        runtime_palette,
                        benchmark_summary,
                        targeting,
                        overlay_settings);
        }
        DrawFooter(renderer, width, height, runtime_palette, state);
        if (state.show_settings_panel) {
            DrawSettingsOverlay(renderer,
                                width,
                                height,
                                runtime_palette,
                                overlay_settings,
                                targeting,
                                state,
                                options.settings_path,
                                window_width,
                                window_height,
                                window_x,
                                window_y);
        }

        SDL_RenderPresent(renderer);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    stop_target_query_edit();
    persist_overlay_settings();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    const AppOptions options = ParseArgs(argc, argv);
    if (options.list_targets) {
        return ListTargets();
    }
    if (options.smoke_test) {
        return RunSmokeTest(options);
    }
    return RunWindow(options);
}
