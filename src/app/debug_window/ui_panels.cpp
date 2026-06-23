#include "ui_panels.h"
#include "bitmap_font.h"
#include "renderer.h"
#include "targeting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "framewatch/overlay/overlay_settings.h"

namespace dw {

namespace {

// Maps FPS to a health color relative to target: at/above = green, degraded = amber, far below = red.
SDL_Color FrameHealthColor(double fps, int target_fps, const Palette& palette) {
    const double target = static_cast<double>(std::max(1, target_fps));
    const double ratio = fps / target;
    if (ratio >= 0.95) return palette.accent;
    if (ratio >= 0.5)  return palette.warning;
    return palette.danger;
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

void DrawSettingsButton(SDL_Renderer* renderer, const Palette& palette, const SettingsPanelButton& button) {
    const SDL_Color accent = button.accent ? palette.accent : palette.accent_2;
    FillRect(renderer, button.rect, SDL_Color{18, 24, 34, 240});
    DrawRect(renderer, button.rect, accent);
    const int label_width = TextWidth(button.label, 1);
    const int text_x = button.rect.x + std::max(12, (button.rect.w - label_width) / 2);
    const int text_y = button.rect.y + std::max(8, (button.rect.h - 8) / 2);
    DrawText(renderer, text_x, text_y, 1, palette.text_primary, button.label);
}

}  // namespace

int ComputeSettingsVisibleRows(int height) {
    const int max_panel_height = std::max(320, height - (kSettingsPanelMargin * 2));
    const int buttons_height = (kSettingsButtonHeight * 5) + (kSettingsButtonGap * 4);
    const int target_list_y = kSettingsButtonStartY + buttons_height + 12;
    const int reserved_height = target_list_y + kSettingsTargetListHeaderHeight + 12 +
                                kSettingsInfoHeight + kSettingsBottomPadding;
    return std::clamp((max_panel_height - reserved_height) / kSettingsTargetRowStride,
                      1, kSettingsMaxVisibleRows);
}

int ClampTargetListStartIndex(const TargetingState& targeting, int visible_rows, int start_index) {
    if (targeting.windows.empty()) return 0;
    const int max_start = std::max(0, static_cast<int>(targeting.windows.size()) - visible_rows);
    return std::clamp(start_index, 0, max_start);
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
    const int query_input_width = query_row_width - (query_actions_width * 2) - (query_actions_gap * 2);
    layout.query_rect =
        SDL_Rect{layout.panel_rect.x + 18, layout.panel_rect.y + 46, query_input_width, 34};
    layout.query_apply_rect = SDL_Rect{layout.query_rect.x + layout.query_rect.w + query_actions_gap,
                                       layout.query_rect.y, query_actions_width, 34};
    layout.query_clear_rect =
        SDL_Rect{layout.query_apply_rect.x + query_actions_width + query_actions_gap,
                 layout.query_rect.y, query_actions_width, 34};

    const int button_width = (panel_width - 54) / 2;
    const int button_origin_y = layout.panel_rect.y + kSettingsButtonStartY;

    auto add_button = [&](int row, int column, SettingsPanelAction action,
                          std::string label, bool accent = false) {
        layout.buttons.push_back(SettingsPanelButton{
            SDL_Rect{layout.panel_rect.x + 18 + column * (button_width + kSettingsButtonGap),
                     button_origin_y + row * (kSettingsButtonHeight + kSettingsButtonGap),
                     button_width, kSettingsButtonHeight},
            action, std::move(label), accent,
        });
    };

    add_button(0, 0, SettingsPanelAction::ToggleGraph,
               std::string("GRAPH ") + (settings.show_graph ? "ON" : "OFF"), settings.show_graph);
    add_button(0, 1, SettingsPanelAction::ToggleSidebar,
               std::string("SIDEBAR ") + (settings.show_sidebar ? "ON" : "OFF"), settings.show_sidebar);
    add_button(1, 0, SettingsPanelAction::ToggleFollow,
               std::string("FOLLOW ") + (targeting.follow_enabled ? "ON" : "OFF"), targeting.follow_enabled);
    add_button(1, 1, SettingsPanelAction::CycleDock,
               std::string("DOCK ") + std::string(framewatch::OverlayDockAnchorName(settings.dock_anchor)));
    add_button(2, 0, SettingsPanelAction::OpacityDown, "OPACITY -");
    add_button(2, 1, SettingsPanelAction::OpacityUp, "OPACITY +");
    add_button(3, 0, SettingsPanelAction::TargetNext, "TARGET NEXT");
    add_button(3, 1, SettingsPanelAction::TargetFront, "TARGET FRONT");
    add_button(4, 0, SettingsPanelAction::ClearTarget, "CLEAR TARGET");
    add_button(4, 1, SettingsPanelAction::ResetDefaults, "RESET DEFAULTS");

    layout.target_list_rect = SDL_Rect{
        layout.panel_rect.x + 18, layout.panel_rect.y + target_list_y,
        panel_width - 36, target_list_height,
    };
    layout.target_page_prev_rect = SDL_Rect{
        layout.target_list_rect.x + layout.target_list_rect.w - 54,
        layout.target_list_rect.y + 4, 20, 14,
    };
    layout.target_page_next_rect = SDL_Rect{
        layout.target_list_rect.x + layout.target_list_rect.w - 28,
        layout.target_list_rect.y + 4, 20, 14,
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
            if (window_index >= static_cast<int>(targeting.windows.size())) break;
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

void DrawHeader(SDL_Renderer* renderer,
                int width,
                const Palette& palette,
                const WindowState& state,
                const framewatch::BenchmarkSummary& benchmark,
                const TargetingState& targeting,
                double over_budget_ratio,
                double stutter_pulse) {
    const SDL_Rect rect{24, 24, width - 48, 88};
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer, rect.x + 20, rect.y + 18, 3, palette.text_primary, "FRAMEWATCH MINI");
    DrawText(renderer, rect.x + 22, rect.y + 50, 2, palette.text_muted, "DEBUG BENCH WINDOW");

    const SDL_Color state_color = state.running ? palette.accent : palette.warning;
    const SDL_Rect run_chip{rect.x + rect.w - 360, rect.y + 18, 104, 28};
    FillRect(renderer, run_chip, SDL_Color{state_color.r, state_color.g, state_color.b, 48});
    DrawRect(renderer, run_chip, state_color);
    DrawText(renderer, run_chip.x + 12, run_chip.y + 7, 2, state_color,
             state.running ? "RUN" : "PAUSE");

    const SDL_Color bench_color = benchmark.active ? palette.danger : palette.accent_2;
    const SDL_Rect bench_chip{rect.x + rect.w - 240, rect.y + 18, 96, 28};
    FillRect(renderer, bench_chip, SDL_Color{bench_color.r, bench_color.g, bench_color.b, 48});
    DrawRect(renderer, bench_chip, bench_color);
    DrawText(renderer, bench_chip.x + 12, bench_chip.y + 7, 2, bench_color,
             benchmark.active ? "REC" : (benchmark.has_data ? "READY" : "IDLE"));

    const SDL_Color target_color =
        CurrentTarget(targeting).has_value() ? palette.accent : palette.text_muted;
    const SDL_Rect target_chip{rect.x + rect.w - 128, rect.y + 18, 108, 28};
    FillRect(renderer, target_chip, SDL_Color{target_color.r, target_color.g, target_color.b, 40});
    DrawRect(renderer, target_chip, target_color);
    DrawText(renderer, target_chip.x + 12, target_chip.y + 7, 2, target_color,
             CurrentTarget(targeting).has_value() ? "TARGET" : "NO TARGET");

    // Frame-budget health chip: share of recent frames over budget, pulsing red on stutter.
    const SDL_Color over_color = over_budget_ratio <= 0.05  ? palette.accent
                                 : over_budget_ratio <= 0.25 ? palette.warning
                                                             : palette.danger;
    std::uint8_t over_fill_alpha = 48;
    if (stutter_pulse > 0.0) {
        over_fill_alpha = static_cast<std::uint8_t>(
            std::clamp(48L + std::lround(160.0 * stutter_pulse), 0L, 255L));
    }
    const SDL_Rect over_chip{rect.x + rect.w - 496, rect.y + 18, 128, 28};
    FillRect(renderer, over_chip, SDL_Color{over_color.r, over_color.g, over_color.b, over_fill_alpha});
    DrawRect(renderer, over_chip, over_color);
    const int over_pct =
        static_cast<int>(std::lround(std::clamp(over_budget_ratio, 0.0, 1.0) * 100.0));
    DrawText(renderer, over_chip.x + 10, over_chip.y + 7, 2, over_color,
             std::string("OVER ") + std::to_string(over_pct) + "%");
}

void DrawStatsGrid(SDL_Renderer* renderer,
                   int width,
                   const Palette& palette,
                   const framewatch::MetricsSnapshot& live_metrics,
                   const framewatch::BenchmarkSummary& benchmark,
                   const TargetingState& targeting,
                   int target_fps) {
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
        palette.accent, palette.accent_2, palette.warning, palette.danger,
        benchmark.active ? palette.danger : palette.accent_2,
        palette.accent, palette.accent_2,
        CurrentTarget(targeting).has_value() ? palette.accent : palette.text_muted,
    };

    // FPS cards (first 4) are colored by health vs target; others use fixed accents.
    const std::array<double, 8> health_fps{
        live_metrics.current_fps, live_metrics.average_fps,
        live_metrics.one_percent_low_fps, live_metrics.point_one_percent_low_fps,
        -1.0, -1.0, -1.0, -1.0,
    };

    for (std::size_t i = 0; i < items.size(); ++i) {
        const int row = static_cast<int>(i) / columns;
        const int column = static_cast<int>(i) % columns;
        const SDL_Rect rect{
            24 + column * (card_width + horizontal_gap),
            top + row * (card_height + vertical_gap),
            card_width, card_height,
        };

        SDL_Color accent = accents[i];
        SDL_Color value_color = palette.text_primary;
        if (health_fps[i] > 0.0) {
            const SDL_Color health = FrameHealthColor(health_fps[i], target_fps, palette);
            accent = health;
            value_color = health;
        }

        DrawCard(renderer, rect, palette.panel, palette.panel_border, accent,
                 palette.text_muted, value_color, items[i].first, items[i].second);
    }
}

void DrawGraph(SDL_Renderer* renderer,
               const SDL_Rect& rect,
               const Palette& palette,
               const framewatch::OverlaySnapshot& snapshot,
               std::string_view label,
               int target_fps,
               double stutter_pulse) {
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);
    FillRect(renderer, SDL_Rect{rect.x, rect.y, rect.w, 4}, palette.accent_2);
    DrawText(renderer, rect.x + 18, rect.y + 16, 2, palette.text_primary, label);

    double avg_ms = 0.0;
    if (!snapshot.graph.empty()) {
        for (const auto& point : snapshot.graph) avg_ms += point.frametime_ms;
        avg_ms /= static_cast<double>(snapshot.graph.size());
    }
    const std::string readout = std::string("MS ") + FormatDouble(snapshot.graph_min_ms, 1) +
                                " / " + FormatDouble(avg_ms, 1) + " / " +
                                FormatDouble(snapshot.graph_max_ms, 1);
    DrawText(renderer, rect.x + rect.w - TextWidth(readout, 2) - 18, rect.y + 16,
             2, palette.text_muted, readout);

    const SDL_Rect plot_rect{rect.x + 18, rect.y + 52, rect.w - 36, rect.h - 78};
    FillRect(renderer, plot_rect, SDL_Color{10, 15, 25, 255});
    DrawRect(renderer, plot_rect, palette.grid);

    // Pulsing red border on stutter.
    if (stutter_pulse > 0.0) {
        const std::uint8_t alpha =
            static_cast<std::uint8_t>(std::clamp(std::lround(220.0 * stutter_pulse), 0L, 255L));
        SetDrawColor(renderer, SDL_Color{palette.danger.r, palette.danger.g, palette.danger.b, alpha});
        SDL_RenderDrawRect(renderer, &plot_rect);
        const SDL_Rect inset{plot_rect.x + 1, plot_rect.y + 1, plot_rect.w - 2, plot_rect.h - 2};
        SDL_RenderDrawRect(renderer, &inset);
    }

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

    const double range = std::max(0.001, snapshot.graph_max_ms - snapshot.graph_min_ms);
    const auto ms_to_y = [&](double ms) {
        const double normalized = std::clamp((ms - snapshot.graph_min_ms) / range, 0.0, 1.0);
        return plot_rect.y + plot_rect.h - static_cast<int>(std::lround(normalized * plot_rect.h));
    };

    // Frame-budget reference lines for 30/60/120 FPS targets visible in the current range.
    struct FrameBudget { double fps; SDL_Color color; };
    const std::array<FrameBudget, 3> budgets{{
        {30.0, palette.danger}, {60.0, palette.accent}, {120.0, palette.accent_2},
    }};
    for (const FrameBudget& budget : budgets) {
        const double budget_ms = 1'000.0 / budget.fps;
        if (budget_ms < snapshot.graph_min_ms || budget_ms > snapshot.graph_max_ms) continue;
        const int budget_y = ms_to_y(budget_ms);
        SetDrawColor(renderer, SDL_Color{budget.color.r, budget.color.g, budget.color.b, 80});
        for (int x = plot_rect.x; x < plot_rect.x + plot_rect.w; x += 8) {
            SDL_RenderDrawLine(renderer, x, budget_y,
                               std::min(x + 4, plot_rect.x + plot_rect.w), budget_y);
        }
        DrawText(renderer, plot_rect.x + 4, budget_y - 12, 1, budget.color,
                 std::to_string(static_cast<int>(std::lround(budget.fps))));
    }

    // Emphasized solid line for the user-configured target FPS.
    const double target_ms = 1'000.0 / static_cast<double>(std::max(1, target_fps));
    if (target_ms >= snapshot.graph_min_ms && target_ms <= snapshot.graph_max_ms) {
        const int target_y = ms_to_y(target_ms);
        SetDrawColor(renderer, palette.accent);
        SDL_RenderDrawLine(renderer, plot_rect.x, target_y, plot_rect.x + plot_rect.w, target_y);
        SDL_RenderDrawLine(renderer, plot_rect.x, target_y - 1, plot_rect.x + plot_rect.w, target_y - 1);
        const std::string tag = std::string("TARGET ") + std::to_string(target_fps);
        DrawText(renderer, plot_rect.x + plot_rect.w - TextWidth(tag, 1) - 6, target_y - 12,
                 1, palette.accent, tag);
    }

    if (snapshot.graph.size() < 2) {
        DrawText(renderer, plot_rect.x + 20, plot_rect.y + plot_rect.h / 2 - 8,
                 2, palette.text_muted, "COLLECTING FRAME DATA");
        return;
    }

    const double spike_threshold =
        snapshot.graph_min_ms + ((snapshot.graph_max_ms - snapshot.graph_min_ms) * 0.75);

    const auto point_x = [&](const framewatch::OverlayGraphPoint& point) {
        return plot_rect.x + static_cast<int>(std::lround(point.x * plot_rect.w));
    };
    const auto point_y = [&](const framewatch::OverlayGraphPoint& point) {
        return plot_rect.y + plot_rect.h - static_cast<int>(std::lround(point.y * plot_rect.h));
    };
    const int plot_bottom = plot_rect.y + plot_rect.h;

    // Filled area under the curve — one vertical line per pixel column to avoid alpha banding.
    for (std::size_t i = 1; i < snapshot.graph.size(); ++i) {
        const int xa = point_x(snapshot.graph[i - 1]);
        const int ya = point_y(snapshot.graph[i - 1]);
        const int xb = point_x(snapshot.graph[i]);
        const int yb = point_y(snapshot.graph[i]);
        SetDrawColor(renderer, SDL_Color{palette.accent_2.r, palette.accent_2.g, palette.accent_2.b, 38});
        for (int x = xa; x <= xb; ++x) {
            const double t = (xb > xa) ? static_cast<double>(x - xa) / (xb - xa) : 0.0;
            const int y = ya + static_cast<int>(std::lround((yb - ya) * t));
            SDL_RenderDrawLine(renderer, x, y, x, plot_bottom);
        }
    }

    // Thick polyline colored by spike severity.
    for (std::size_t i = 1; i < snapshot.graph.size(); ++i) {
        const int x1 = point_x(snapshot.graph[i - 1]);
        const int y1 = point_y(snapshot.graph[i - 1]);
        const int x2 = point_x(snapshot.graph[i]);
        const int y2 = point_y(snapshot.graph[i]);
        const SDL_Color line_color =
            snapshot.graph[i].frametime_ms >= spike_threshold ? palette.warning : palette.accent_2;
        SetDrawColor(renderer, line_color);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        SDL_RenderDrawLine(renderer, x1, y1 - 1, x2, y2 - 1);
        SDL_RenderDrawLine(renderer, x1, y1 + 1, x2, y2 + 1);
    }

    const auto& latest = snapshot.graph.back();
    const int latest_x = point_x(latest);
    const int latest_y = point_y(latest);
    FillRect(renderer, SDL_Rect{latest_x - 3, latest_y - 3, 7, 7}, palette.accent);
    DrawRect(renderer, SDL_Rect{latest_x - 5, latest_y - 5, 11, 11}, palette.accent);
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
        "B START/STOP", "R RESET", "E EXPORT",
    };
    DrawInfoPanel(renderer, benchmark_rect, palette,
                  benchmark.active ? palette.danger : palette.accent_2, "BENCHMARK", benchmark_lines);

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
        target_lines.push_back(std::string("TARGET FPS ") + std::to_string(settings.target_fps));
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
        target_lines.push_back("P TARGET FPS  S PANEL  T QUERY  D RESET");
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
    DrawText(renderer, rect.x + 16, rect.y + 6, 1, palette.text_muted,
             "SPACE PAUSE  B BENCH  R RESET  E EXPORT  S SETTINGS  T QUERY  TAB/ARROWS TARGET\n"
             "G FRONT  F FOLLOW  N CLEAR  C DOCK  [ ] OPACITY  V GRAPH  I SIDE  PG/WHEEL LIST  ESC QUIT");
    const SDL_Color status_color =
        (state.status_until > SteadyClock::now()) ? palette.accent : palette.text_muted;
    const std::string status = SanitizeUiText(state.status_text, 28);
    DrawText(renderer, rect.x + rect.w - TextWidth(status, 2) - 16, rect.y + 14,
             2, status_color, status);
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
    FillRect(renderer, SDL_Rect{layout.panel_rect.x, layout.panel_rect.y, layout.panel_rect.w, 4},
             palette.warning);

    DrawText(renderer, layout.panel_rect.x + 16, layout.panel_rect.y + 14,
             2, palette.text_primary, "SETTINGS");
    DrawText(renderer, layout.panel_rect.x + layout.panel_rect.w - 154, layout.panel_rect.y + 16,
             1, palette.text_muted,
             state.editing_target_query ? "ENTER APPLY  ESC CANCEL" : "QUERY / APPLY / CLEAR");

    FillRect(renderer, layout.query_rect,
             state.editing_target_query ? SDL_Color{22, 33, 49, 255} : SDL_Color{14, 20, 28, 255});
    DrawRect(renderer, layout.query_rect,
             state.editing_target_query ? palette.accent : palette.grid);
    DrawText(renderer, layout.query_rect.x + 10, layout.query_rect.y + 9, 1, palette.text_primary,
             state.editing_target_query
                 ? SanitizeUiText(state.target_query_buffer + "_", 46)
                 : std::string("QUERY ") +
                       (targeting.title_query.empty() ? "NONE" : SanitizeUiText(targeting.title_query, 38)));
    DrawSettingsButton(renderer, palette,
                       SettingsPanelButton{layout.query_apply_rect, SettingsPanelAction::ToggleGraph,
                                          "APPLY",
                                          state.editing_target_query || !targeting.title_query.empty()});
    DrawSettingsButton(renderer, palette,
                       SettingsPanelButton{layout.query_clear_rect, SettingsPanelAction::ClearTarget,
                                          "CLEAR",
                                          state.editing_target_query || !targeting.title_query.empty()});

    for (const SettingsPanelButton& button : layout.buttons) {
        DrawSettingsButton(renderer, palette, button);
    }

    FillRect(renderer, layout.target_list_rect, SDL_Color{12, 18, 26, 255});
    DrawRect(renderer, layout.target_list_rect, palette.grid);
    DrawText(renderer, layout.target_list_rect.x + 10, layout.target_list_rect.y + 7,
             1, palette.text_muted, "VISIBLE WINDOWS");

    FillRect(renderer, layout.target_page_prev_rect,
             layout.has_prev_page ? SDL_Color{18, 28, 40, 255} : SDL_Color{14, 20, 28, 180});
    DrawRect(renderer, layout.target_page_prev_rect,
             layout.has_prev_page ? palette.accent_2 : palette.grid);
    DrawText(renderer, layout.target_page_prev_rect.x + 7, layout.target_page_prev_rect.y + 4,
             1, layout.has_prev_page ? palette.text_primary : palette.text_muted, "<");
    FillRect(renderer, layout.target_page_next_rect,
             layout.has_next_page ? SDL_Color{18, 28, 40, 255} : SDL_Color{14, 20, 28, 180});
    DrawRect(renderer, layout.target_page_next_rect,
             layout.has_next_page ? palette.accent_2 : palette.grid);
    DrawText(renderer, layout.target_page_next_rect.x + 7, layout.target_page_next_rect.y + 4,
             1, layout.has_next_page ? palette.text_primary : palette.text_muted, ">");

    const std::string rows_label =
        layout.target_rows.empty()
            ? std::string("ROWS 0/0")
            : std::string("ROWS ") + std::to_string(layout.first_visible_index + 1) + "-" +
                  std::to_string(layout.first_visible_index + layout.target_rows.size()) + "/" +
                  std::to_string(targeting.windows.size());
    DrawText(renderer,
             layout.target_list_rect.x + layout.target_list_rect.w - 196,
             layout.target_list_rect.y + 7,
             1, palette.text_muted, rows_label);

    if (layout.target_rows.empty()) {
        DrawText(renderer, layout.target_list_rect.x + 10, layout.target_list_rect.y + 30,
                 1, palette.text_muted, "NO TARGETABLE WINDOWS");
    } else {
        for (const SettingsPanelTargetRow& row : layout.target_rows) {
            const bool selected = row.window_index == targeting.selected_index;
            const bool hovered = row.window_index == state.hovered_target_index;
            const SDL_Color row_border =
                selected ? palette.accent : (hovered ? palette.accent_2 : palette.grid);
            const SDL_Color row_fill = selected ? SDL_Color{20, 52, 48, 255}
                                                : (hovered ? SDL_Color{24, 35, 52, 255}
                                                           : SDL_Color{16, 23, 33, 255});
            FillRect(renderer, row.rect, row_fill);
            DrawRect(renderer, row.rect, row_border);
            const auto& window = targeting.windows[static_cast<std::size_t>(row.window_index)];
            const std::string row_label =
                std::to_string(row.window_index + 1) + ". " + TargetLabel(window) + "  " +
                std::to_string(window.width) + "x" + std::to_string(window.height);
            DrawText(renderer, row.rect.x + 8, row.rect.y + 6, 1,
                     selected ? palette.text_primary : palette.text_muted,
                     SanitizeUiText(row_label, 54));
        }
    }

    const auto current_target = CurrentTarget(targeting);
    const bool preview_from_hover =
        state.hovered_target_index >= 0 &&
        state.hovered_target_index < static_cast<int>(targeting.windows.size());
    const auto preview_target =
        preview_from_hover
            ? std::optional<framewatch::DesktopWindowInfo>{
                  targeting.windows[static_cast<std::size_t>(state.hovered_target_index)]}
            : current_target;
    const int info_y = layout.target_list_rect.y + layout.target_list_rect.h + 16;
    DrawText(renderer, layout.panel_rect.x + 18, info_y, 1, palette.text_muted,
             std::string("WINDOW ") + std::to_string(window_width) + "X" +
                 std::to_string(window_height) + "  POS " +
                 std::to_string(window_x) + "," + std::to_string(window_y));
    DrawText(renderer, layout.panel_rect.x + 18, info_y + 18, 1, palette.text_muted,
             preview_target.has_value()
                 ? std::string(preview_from_hover ? "HOVER " : "SELECTED ") +
                       SanitizeUiText(TargetLabel(*preview_target), 38)
                 : "SELECTED NONE  UP/DOWN/HOME/END TO MOVE");
    DrawText(renderer, layout.panel_rect.x + 18, info_y + 36, 1, palette.text_muted,
             preview_target.has_value()
                 ? std::string("OWNER ") +
                       SanitizeUiText(preview_target->owner_name.empty()
                                          ? std::string("UNKNOWN") : preview_target->owner_name, 42)
                 : std::string("VISIBLE TARGETS ") + std::to_string(targeting.windows.size()) +
                       "  WHEEL/PAGE TO SCROLL");
    DrawText(renderer, layout.panel_rect.x + 18, info_y + 54, 1, palette.text_muted,
             preview_target.has_value()
                 ? std::string("TITLE ") +
                       SanitizeUiText(preview_target->title.empty()
                                          ? std::string("UNTITLED") : preview_target->title, 42)
                 : "CLICK ROW TO LOCK  TAB/ARROWS TO NAVIGATE");
    DrawText(renderer, layout.panel_rect.x + 18, info_y + 72, 1, palette.text_muted,
             preview_target.has_value()
                 ? std::string("BOUNDS ") + std::to_string(preview_target->x) + "," +
                       std::to_string(preview_target->y) + "  " +
                       std::to_string(preview_target->width) + "X" +
                       std::to_string(preview_target->height) + "  ID " +
                       std::to_string(preview_target->id)
                 : std::string("FILE ") + SanitizeUiText(settings_path.string(), 52));
    DrawText(renderer, layout.panel_rect.x + 18, info_y + 90, 1, palette.text_muted,
             std::string("FILE ") + SanitizeUiText(settings_path.string(), 52));
}

}  // namespace dw
