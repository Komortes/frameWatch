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
    std::string status_text{"SIMULATION RUNNING"};
    SteadyClock::time_point status_until{};
    SteadyClock::time_point last_step_at{SteadyClock::now()};
    SteadyClock::time_point last_title_update_at{SteadyClock::now()};
    SteadyClock::time_point last_target_refresh_at{SteadyClock::now() - std::chrono::seconds(1)};
    SteadyClock::time_point last_follow_sync_at{SteadyClock::now() - std::chrono::seconds(1)};
};

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
        target_lines.push_back("D RESET  JSON AUTO-SAVE");
    }
    DrawInfoPanel(renderer, target_rect, palette, palette.accent, "TARGET WINDOW", target_lines);
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
             "SPACE PAUSE  B BENCH  R RESET  E EXPORT  TAB CYCLE  SHIFT+TAB BACK\nG FRONT  F FOLLOW  N CLEAR  C DOCK  [ ] OPACITY  V GRAPH  I SIDE  D DEFAULTS  ESC QUIT");

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

    SDL_Window* window = SDL_CreateWindow(kSelfTitleMarker.data(),
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          1180,
                                          760,
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
    framewatch::OverlaySettings overlay_settings;
    if (const auto loaded_settings = framewatch::LoadOverlaySettings(options.settings_path)) {
        overlay_settings = *loaded_settings;
    }
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

    auto persist_overlay_settings = [&]() {
        overlay_settings.follow_target_window = targeting.follow_enabled;
        overlay_settings.target_window_query = targeting.title_query;
        if (!framewatch::SaveOverlaySettings(overlay_settings, options.settings_path)) {
            SetStatus(state, "SETTINGS SAVE FAILED", std::chrono::seconds(3));
        }
    };

    while (!state.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                state.quit = true;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                const bool shift_held = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        state.quit = true;
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
                        SetStatus(state,
                                  CurrentTarget(targeting).has_value() ? "TARGET CHANGED"
                                                                       : "NO TARGET AVAILABLE",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_g:
                        PickFrontmostTarget(targeting, kSelfTitleMarker);
                        if (const auto target = CurrentTarget(targeting)) {
                            targeting.title_query = TargetQueryForPersistence(*target);
                        }
                        SetStatus(state,
                                  CurrentTarget(targeting).has_value() ? "FRONTMOST TARGET LOCKED"
                                                                       : "NO TARGET FOUND",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_f:
                        targeting.follow_enabled = !targeting.follow_enabled;
                        SetStatus(state,
                                  targeting.follow_enabled ? "FOLLOW TARGET ON" : "FOLLOW TARGET OFF",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_n:
                        targeting.selected_index = -1;
                        targeting.title_query.clear();
                        SetStatus(state, "TARGET CLEARED", std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_c:
                        overlay_settings.dock_anchor =
                            framewatch::CycleOverlayDockAnchor(overlay_settings.dock_anchor);
                        SetStatus(state,
                                  std::string("DOCK ") +
                                      std::string(framewatch::OverlayDockAnchorName(
                                          overlay_settings.dock_anchor)),
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_LEFTBRACKET:
                        framewatch::AdjustOverlayOpacity(overlay_settings, -0.10);
                        SetStatus(state,
                                  std::string("OPACITY ") +
                                      std::to_string(static_cast<int>(std::lround(
                                          overlay_settings.panel_opacity * 100.0))) +
                                      "%",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_RIGHTBRACKET:
                        framewatch::AdjustOverlayOpacity(overlay_settings, 0.10);
                        SetStatus(state,
                                  std::string("OPACITY ") +
                                      std::to_string(static_cast<int>(std::lround(
                                          overlay_settings.panel_opacity * 100.0))) +
                                      "%",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_v:
                        overlay_settings.show_graph = !overlay_settings.show_graph;
                        SetStatus(state,
                                  overlay_settings.show_graph ? "GRAPH VISIBLE" : "GRAPH HIDDEN",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_i:
                        overlay_settings.show_sidebar = !overlay_settings.show_sidebar;
                        SetStatus(state,
                                  overlay_settings.show_sidebar ? "SIDEBAR VISIBLE"
                                                                : "SIDEBAR HIDDEN",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_d:
                        overlay_settings = framewatch::OverlaySettings{};
                        targeting.follow_enabled = overlay_settings.follow_target_window;
                        targeting.title_query = overlay_settings.target_window_query;
                        targeting.selected_index = -1;
                        SetStatus(state, "SETTINGS RESET", std::chrono::seconds(2));
                        persist_overlay_settings();
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

        SDL_RenderPresent(renderer);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

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
