#include "debug_window/types.h"
#include "debug_window/renderer.h"
#include "debug_window/targeting.h"
#include "debug_window/ui_panels.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "framewatch/overlay/overlay_settings.h"
#include "framewatch/platform/window_targeting.h"
#include "framewatch/session/performance_session.h"

namespace {

using namespace dw;

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

void SetStatus(WindowState& state, std::string message, std::chrono::seconds ttl) {
    state.status_text = std::move(message);
    state.status_until = SteadyClock::now() + ttl;
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
        const auto& w = windows[i];
        std::cout << i << ": [" << w.owner_name << "] " << w.title
                  << " @ " << w.x << "," << w.y << " " << w.width << "x" << w.height << '\n';
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
    if (const auto loaded = framewatch::LoadOverlaySettings(options.settings_path)) {
        overlay_settings = *loaded;
    }

    const int initial_window_x = overlay_settings.window_x.value_or(SDL_WINDOWPOS_CENTERED);
    const int initial_window_y = overlay_settings.window_y.value_or(SDL_WINDOWPOS_CENTERED);
    SDL_Window* window = SDL_CreateWindow(kSelfTitleMarker.data(),
                                          initial_window_x, initial_window_y,
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
    targeting.follow_enabled = options.follow_target || overlay_settings.follow_target_window;
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
        int width = 0, height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        return ComputeSettingsVisibleRows(height);
    };
    auto clamp_target_list_start = [&]() {
        state.target_list_start_index =
            ClampTargetListStartIndex(targeting, settings_visible_rows(),
                                     state.target_list_start_index);
    };
    auto clear_hovered_target = [&]() { state.hovered_target_index = -1; };
    auto ensure_selected_target_visible = [&]() {
        const int visible_rows = settings_visible_rows();
        if (targeting.selected_index >= 0) {
            if (targeting.selected_index < state.target_list_start_index) {
                state.target_list_start_index = targeting.selected_index;
            } else if (targeting.selected_index >= (state.target_list_start_index + visible_rows)) {
                state.target_list_start_index = targeting.selected_index - visible_rows + 1;
            }
        }
        state.target_list_start_index =
            ClampTargetListStartIndex(targeting, visible_rows, state.target_list_start_index);
    };
    auto scroll_target_list = [&](int delta_rows) {
        if (targeting.windows.empty() || delta_rows == 0) return;
        state.target_list_start_index =
            ClampTargetListStartIndex(targeting, settings_visible_rows(),
                                     state.target_list_start_index + delta_rows);
    };
    auto page_target_list = [&](int direction) {
        const int visible_rows = settings_visible_rows();
        scroll_target_list(direction * std::max(1, visible_rows - 1));
    };
    ensure_selected_target_visible();

    auto persist_overlay_settings = [&]() {
        int window_width = 0, window_height = 0;
        SDL_GetWindowSize(window, &window_width, &window_height);
        overlay_settings.window_width = std::max(window_width, 640);
        overlay_settings.window_height = std::max(window_height, 420);
        int window_x = 0, window_y = 0;
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
        if (!state.editing_target_query) return;
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
        clear_hovered_target();
        if (stop_edit_mode) stop_target_query_edit();
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
    auto apply_target_query_edit = [&]() { apply_target_query(state.target_query_buffer, true); };
    auto apply_active_target_query = [&]() { apply_target_query(targeting.title_query, false); };
    auto cancel_target_query_edit = [&]() {
        stop_target_query_edit();
        SetStatus(state, "TARGET QUERY CANCELED", std::chrono::seconds(2));
    };
    auto clear_target_query = [&]() {
        stop_target_query_edit();
        clear_hovered_target();
        targeting.title_query.clear();
        targeting.selected_index = -1;
        state.target_list_start_index = 0;
        RefreshTargets(targeting, kSelfTitleMarker);
        SetStatus(state, "TARGET QUERY CLEARED", std::chrono::seconds(2));
        persist_overlay_settings();
    };
    auto select_target_row = [&](int window_index) {
        if (window_index < 0 || window_index >= static_cast<int>(targeting.windows.size())) return;
        targeting.selected_index = window_index;
        state.hovered_target_index = window_index;
        targeting.title_query =
            TargetQueryForPersistence(targeting.windows[static_cast<std::size_t>(window_index)]);
        ensure_selected_target_visible();
        SetStatus(state, "TARGET SELECTED", std::chrono::seconds(2));
        persist_overlay_settings();
    };
    auto select_target_delta = [&](int direction) {
        if (targeting.windows.empty()) {
            SetStatus(state, "NO TARGET AVAILABLE", std::chrono::seconds(2));
            return;
        }
        const int current_index = targeting.selected_index;
        const int next_index =
            (current_index < 0)
                ? (direction >= 0 ? 0 : static_cast<int>(targeting.windows.size()) - 1)
                : std::clamp(current_index + direction,
                             0, static_cast<int>(targeting.windows.size()) - 1);
        select_target_row(next_index);
    };
    auto select_target_edge = [&](bool select_last) {
        if (targeting.windows.empty()) {
            SetStatus(state, "NO TARGET AVAILABLE", std::chrono::seconds(2));
            return;
        }
        select_target_row(select_last ? static_cast<int>(targeting.windows.size()) - 1 : 0);
    };

    auto perform_settings_action = [&](SettingsPanelAction action) {
        switch (action) {
            case SettingsPanelAction::ToggleGraph:
                overlay_settings.show_graph = !overlay_settings.show_graph;
                SetStatus(state, overlay_settings.show_graph ? "GRAPH VISIBLE" : "GRAPH HIDDEN",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::ToggleSidebar:
                overlay_settings.show_sidebar = !overlay_settings.show_sidebar;
                SetStatus(state, overlay_settings.show_sidebar ? "SIDEBAR VISIBLE" : "SIDEBAR HIDDEN",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::ToggleFollow:
                targeting.follow_enabled = !targeting.follow_enabled;
                SetStatus(state, targeting.follow_enabled ? "FOLLOW TARGET ON" : "FOLLOW TARGET OFF",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::CycleDock:
                overlay_settings.dock_anchor =
                    framewatch::CycleOverlayDockAnchor(overlay_settings.dock_anchor);
                SetStatus(state,
                          std::string("DOCK ") + std::string(framewatch::OverlayDockAnchorName(
                                                                 overlay_settings.dock_anchor)),
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::OpacityDown:
                framewatch::AdjustOverlayOpacity(overlay_settings, -0.10);
                SetStatus(state,
                          std::string("OPACITY ") +
                              std::to_string(static_cast<int>(
                                  std::lround(overlay_settings.panel_opacity * 100.0))) + "%",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::OpacityUp:
                framewatch::AdjustOverlayOpacity(overlay_settings, 0.10);
                SetStatus(state,
                          std::string("OPACITY ") +
                              std::to_string(static_cast<int>(
                                  std::lround(overlay_settings.panel_opacity * 100.0))) + "%",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::CycleTargetFps:
                overlay_settings.target_fps =
                    framewatch::CycleTargetFps(overlay_settings.target_fps, 1);
                SetStatus(state,
                          std::string("TARGET FPS ") + std::to_string(overlay_settings.target_fps),
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::TargetNext:
                RefreshTargets(targeting, kSelfTitleMarker);
                CycleTarget(targeting, 1);
                if (const auto target = CurrentTarget(targeting)) {
                    targeting.title_query = TargetQueryForPersistence(*target);
                }
                clear_hovered_target();
                ensure_selected_target_visible();
                SetStatus(state,
                          CurrentTarget(targeting).has_value() ? "TARGET CHANGED" : "NO TARGET AVAILABLE",
                          std::chrono::seconds(2));
                persist_overlay_settings();
                break;
            case SettingsPanelAction::TargetFront:
                PickFrontmostTarget(targeting, kSelfTitleMarker);
                if (const auto target = CurrentTarget(targeting)) {
                    targeting.title_query = TargetQueryForPersistence(*target);
                }
                clear_hovered_target();
                ensure_selected_target_visible();
                SetStatus(state,
                          CurrentTarget(targeting).has_value() ? "FRONTMOST TARGET LOCKED" : "NO TARGET FOUND",
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
                clear_hovered_target();
                SDL_SetWindowSize(window, overlay_settings.window_width, overlay_settings.window_height);
                SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
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
                    if (ch < 32 || ch > 126 || state.target_query_buffer.size() >= 46) continue;
                    state.target_query_buffer.push_back(static_cast<char>(ch));
                }
            } else if (event.type == SDL_MOUSEWHEEL && state.show_settings_panel &&
                       !state.editing_target_query) {
                int width = 0, height = 0, mouse_x = 0, mouse_y = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                SDL_GetMouseState(&mouse_x, &mouse_y);
                ScaleMouseToRender(window, renderer, mouse_x, mouse_y);
                const SettingsPanelLayout layout =
                    BuildSettingsPanelLayout(width, height, overlay_settings, targeting,
                                            state.target_list_start_index);
                if (PointInRect(mouse_x, mouse_y, layout.target_list_rect)) {
                    scroll_target_list(-event.wheel.y);
                }
            } else if (event.type == SDL_MOUSEMOTION && state.show_settings_panel &&
                       !state.editing_target_query) {
                int width = 0, height = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                const SettingsPanelLayout layout =
                    BuildSettingsPanelLayout(width, height, overlay_settings, targeting,
                                            state.target_list_start_index);
                int mouse_x = event.motion.x, mouse_y = event.motion.y;
                ScaleMouseToRender(window, renderer, mouse_x, mouse_y);
                state.hovered_target_index = -1;
                if (PointInRect(mouse_x, mouse_y, layout.target_list_rect)) {
                    for (const SettingsPanelTargetRow& row : layout.target_rows) {
                        if (PointInRect(mouse_x, mouse_y, row.rect)) {
                            state.hovered_target_index = row.window_index;
                            break;
                        }
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
                       state.show_settings_panel) {
                int width = 0, height = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                const SettingsPanelLayout layout =
                    BuildSettingsPanelLayout(width, height, overlay_settings, targeting,
                                            state.target_list_start_index);
                int mouse_x = event.button.x, mouse_y = event.button.y;
                ScaleMouseToRender(window, renderer, mouse_x, mouse_y);

                if (!PointInRect(mouse_x, mouse_y, layout.panel_rect)) {
                    if (state.editing_target_query) cancel_target_query_edit();
                    clear_hovered_target();
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
                    if (handled) continue;

                    for (const SettingsPanelTargetRow& row : layout.target_rows) {
                        if (PointInRect(mouse_x, mouse_y, row.rect)) {
                            select_target_row(row.window_index);
                            handled = true;
                            break;
                        }
                    }
                    if (handled) continue;

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
                        case SDLK_ESCAPE:   cancel_target_query_edit(); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: apply_target_query_edit(); break;
                        case SDLK_BACKSPACE:
                            if (!state.target_query_buffer.empty()) {
                                state.target_query_buffer.pop_back();
                            }
                            break;
                        default: break;
                    }
                    continue;
                }

                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        if (state.show_settings_panel) {
                            clear_hovered_target();
                            state.show_settings_panel = false;
                            SetStatus(state, "SETTINGS PANEL CLOSED", std::chrono::seconds(2));
                        } else {
                            state.quit = true;
                        }
                        break;
                    case SDLK_s:
                        if (state.show_settings_panel) clear_hovered_target();
                        state.show_settings_panel = !state.show_settings_panel;
                        SetStatus(state,
                                  state.show_settings_panel ? "SETTINGS PANEL OPEN" : "SETTINGS PANEL CLOSED",
                                  std::chrono::seconds(2));
                        break;
                    case SDLK_t: begin_target_query_edit(); break;
                    case SDLK_UP:
                        if (state.show_settings_panel) select_target_delta(-1);
                        break;
                    case SDLK_DOWN:
                        if (state.show_settings_panel) select_target_delta(1);
                        break;
                    case SDLK_HOME:
                        if (state.show_settings_panel) select_target_edge(false);
                        break;
                    case SDLK_END:
                        if (state.show_settings_panel) select_target_edge(true);
                        break;
                    case SDLK_PAGEUP:
                        if (state.show_settings_panel) page_target_list(-1);
                        break;
                    case SDLK_PAGEDOWN:
                        if (state.show_settings_panel) page_target_list(1);
                        break;
                    case SDLK_SPACE:
                        state.running = !state.running;
                        SetStatus(state, state.running ? "SIMULATION RUNNING" : "SIMULATION PAUSED",
                                  std::chrono::seconds(2));
                        break;
                    case SDLK_b:
                        benchmark.ToggleBenchmark();
                        SetStatus(state,
                                  benchmark.IsBenchmarkRecording() ? "BENCHMARK RECORDING" : "BENCHMARK STOPPED",
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
                                  ExportSession(benchmark, options) ? "EXPORT COMPLETE" : "EXPORT FAILED",
                                  std::chrono::seconds(3));
                        break;
                    case SDLK_TAB:
                        RefreshTargets(targeting, kSelfTitleMarker);
                        CycleTarget(targeting, shift_held ? -1 : 1);
                        if (const auto target = CurrentTarget(targeting)) {
                            targeting.title_query = TargetQueryForPersistence(*target);
                        }
                        clear_hovered_target();
                        ensure_selected_target_visible();
                        SetStatus(state,
                                  CurrentTarget(targeting).has_value() ? "TARGET CHANGED" : "NO TARGET AVAILABLE",
                                  std::chrono::seconds(2));
                        persist_overlay_settings();
                        break;
                    case SDLK_g: perform_settings_action(SettingsPanelAction::TargetFront); break;
                    case SDLK_f: perform_settings_action(SettingsPanelAction::ToggleFollow); break;
                    case SDLK_n: perform_settings_action(SettingsPanelAction::ClearTarget); break;
                    case SDLK_c: perform_settings_action(SettingsPanelAction::CycleDock); break;
                    case SDLK_LEFTBRACKET:  perform_settings_action(SettingsPanelAction::OpacityDown); break;
                    case SDLK_RIGHTBRACKET: perform_settings_action(SettingsPanelAction::OpacityUp); break;
                    case SDLK_v: perform_settings_action(SettingsPanelAction::ToggleGraph); break;
                    case SDLK_i: perform_settings_action(SettingsPanelAction::ToggleSidebar); break;
                    case SDLK_d: perform_settings_action(SettingsPanelAction::ResetDefaults); break;
                    case SDLK_p: perform_settings_action(SettingsPanelAction::CycleTargetFps); break;
                    default: break;
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
            clear_hovered_target();
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

        int width = 0, height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        clamp_target_list_start();
        int window_width = 0, window_height = 0, window_x = 0, window_y = 0;
        SDL_GetWindowSize(window, &window_width, &window_height);
        SDL_GetWindowPosition(window, &window_x, &window_y);

        const Palette runtime_palette = ApplyOverlaySettings(palette, overlay_settings);
        const auto graph_snapshot = benchmark.GraphSnapshot();

        // Frame-budget alert: share of recent frames over budget + stutter pulse.
        const double budget_ms =
            1'000.0 / static_cast<double>(std::max(1, overlay_settings.target_fps));
        std::size_t over_budget_frames = 0;
        for (const auto& point : graph_snapshot.graph) {
            if (point.frametime_ms > budget_ms) ++over_budget_frames;
        }
        const double over_budget_ratio =
            graph_snapshot.graph.empty()
                ? 0.0
                : static_cast<double>(over_budget_frames) /
                      static_cast<double>(graph_snapshot.graph.size());
        const bool stutter_active =
            live_metrics.latest_frametime_ms > (budget_ms * kStutterBudgetMultiplier);
        const double pulse_phase =
            0.5 + 0.5 * std::sin(
                            std::chrono::duration<double>(now.time_since_epoch()).count() * 6.0);
        const double stutter_pulse = stutter_active ? pulse_phase : 0.0;

        const int sidebar_width = overlay_settings.show_sidebar ? 272 : 0;
        const int graph_width = overlay_settings.show_sidebar ? width - 344 : width - 48;
        const SDL_Rect graph_rect{24, 364, std::max(320, graph_width), std::max(180, height - 458)};
        const SDL_Rect sidebar_rect{width - 296, 364, sidebar_width, std::max(180, height - 458)};

        DrawGradientBackground(renderer, width, height,
                               runtime_palette.background_top, runtime_palette.background_bottom);
        DrawHeader(renderer, width, runtime_palette, state, benchmark_summary, targeting,
                   over_budget_ratio, stutter_pulse);
        DrawStatsGrid(renderer, width, runtime_palette, live_metrics, benchmark_summary, targeting,
                      overlay_settings.target_fps);
        if (overlay_settings.show_graph) {
            DrawGraph(renderer, graph_rect, runtime_palette, graph_snapshot, benchmark.GraphLabel(),
                      overlay_settings.target_fps, stutter_pulse);
        } else {
            const std::vector<std::string> overlay_lines{
                "GRAPH HIDDEN",
                std::string("DOCK ") +
                    std::string(framewatch::OverlayDockAnchorName(overlay_settings.dock_anchor)),
                std::string("OPACITY ") +
                    std::to_string(static_cast<int>(
                        std::lround(overlay_settings.panel_opacity * 100.0))) + "%",
                std::string("SIDEBAR ") + (overlay_settings.show_sidebar ? "ON" : "OFF"),
                "PRESS V TO SHOW GRAPH",
            };
            DrawInfoPanel(renderer, graph_rect, runtime_palette, runtime_palette.accent_2,
                          "OVERLAY SETTINGS", overlay_lines);
        }
        if (overlay_settings.show_sidebar) {
            DrawSidebar(renderer, sidebar_rect, runtime_palette, benchmark_summary, targeting,
                        overlay_settings);
        }
        DrawFooter(renderer, width, height, runtime_palette, state);
        if (state.show_settings_panel) {
            DrawSettingsOverlay(renderer, width, height, runtime_palette, overlay_settings,
                                targeting, state, options.settings_path,
                                window_width, window_height, window_x, window_y);
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
    if (options.list_targets) return ListTargets();
    if (options.smoke_test)   return RunSmokeTest(options);
    return RunWindow(options);
}
