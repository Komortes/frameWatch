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
#include <vector>

#include "framewatch/core/frametime_tracker.h"
#include "framewatch/core/metrics_engine.h"
#include "framewatch/core/session_logger.h"
#include "framewatch/overlay/overlay_model.h"

namespace {

using SteadyClock = std::chrono::steady_clock;
using Glyph = std::array<std::uint8_t, 7>;

struct AppOptions {
    bool smoke_test{false};
    std::filesystem::path csv_path{"output/framewatch_debug_window.csv"};
    std::filesystem::path json_path{"output/framewatch_debug_window.json"};
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

AppOptions ParseArgs(int argc, char** argv) {
    AppOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke-test") {
            options.smoke_test = true;
        } else if (arg == "--csv" && (i + 1) < argc) {
            options.csv_path = argv[++i];
        } else if (arg == "--json" && (i + 1) < argc) {
            options.json_path = argv[++i];
        }
    }

    return options;
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

void DrawGlyph(SDL_Renderer* renderer,
               int x,
               int y,
               int scale,
               SDL_Color color,
               char ch) {
    const Glyph glyph = GlyphFor(ch);
    SetDrawColor(renderer, color);

    for (int row = 0; row < static_cast<int>(glyph.size()); ++row) {
        for (int col = 0; col < 5; ++col) {
            const bool filled = (glyph[static_cast<std::size_t>(row)] & (1 << (4 - col))) != 0;
            if (!filled) {
                continue;
            }

            const SDL_Rect pixel_rect{
                x + (col * scale),
                y + (row * scale),
                scale,
                scale,
            };
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

struct SyntheticBenchmark {
    framewatch::FrametimeTracker tracker;
    framewatch::MetricsEngine metrics{360};
    framewatch::SessionLogger logger;
    framewatch::OverlayModel overlay_model;
    framewatch::FrameClock::time_point simulated_timestamp{};
    std::mt19937 rng{42};
    std::normal_distribution<double> baseline_frametime_ms{16.6, 0.35};
    std::uint64_t generated_frames{0};

    SyntheticBenchmark() { Reset(); }

    void Reset() {
        tracker.Reset();
        metrics.Reset();
        logger.Clear();
        simulated_timestamp = framewatch::FrameClock::time_point{};
        generated_frames = 0;
        tracker.Capture(simulated_timestamp);
    }

    void Step() {
        ++generated_frames;

        double frametime_ms = std::max(5.0, baseline_frametime_ms(rng));
        if (generated_frames % 120 == 0) {
            frametime_ms += 8.5;
        }
        if (generated_frames % 257 == 0) {
            frametime_ms += 16.0;
        }

        simulated_timestamp += std::chrono::microseconds(
            static_cast<long long>(std::llround(frametime_ms * 1'000.0)));

        if (auto sample = tracker.Capture(simulated_timestamp)) {
            metrics.PushSample(*sample);
            logger.Append(*sample);
        }
    }

    framewatch::MetricsSnapshot Snapshot() const {
        return metrics.Snapshot();
    }

    framewatch::OverlaySnapshot OverlaySnapshot() const {
        return overlay_model.Build(metrics.Snapshot(), metrics.RecentFrametimeHistory());
    }
};

struct WindowState {
    bool running{true};
    bool quit{false};
    std::string status_text{"SIMULATION RUNNING"};
    SteadyClock::time_point status_until{};
    SteadyClock::time_point last_step_at{SteadyClock::now()};
    SteadyClock::time_point last_title_update_at{SteadyClock::now()};
};

void SetStatus(WindowState& state, std::string message, std::chrono::seconds ttl) {
    state.status_text = std::move(message);
    state.status_until = SteadyClock::now() + ttl;
}

void UpdateWindowTitle(SDL_Window* window,
                       const framewatch::MetricsSnapshot& metrics,
                       const WindowState& state) {
    std::ostringstream stream;
    stream << "FrameWatch Mini Debug Window";
    if (metrics.sample_count > 0) {
        stream << " | FPS " << std::fixed << std::setprecision(1) << metrics.current_fps;
        stream << " | 1% LOW " << metrics.one_percent_low_fps;
    }
    stream << (state.running ? " | RUNNING" : " | PAUSED");
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

    const SDL_Rect accent_rect{rect.x, rect.y, rect.w, 4};
    FillRect(renderer, accent_rect, accent_color);

    DrawText(renderer, rect.x + 16, rect.y + 16, 2, label_color, label);
    DrawText(renderer, rect.x + 16, rect.y + 48, 3, value_color, value);
}

void DrawHeader(SDL_Renderer* renderer,
                int width,
                const Palette& palette,
                const framewatch::MetricsSnapshot& metrics,
                const WindowState& state,
                std::size_t sample_count) {
    const SDL_Rect rect{24, 24, width - 48, 88};
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer, rect.x + 20, rect.y + 18, 3, palette.text_primary, "FRAMEWATCH MINI");
    DrawText(renderer, rect.x + 22, rect.y + 50, 2, palette.text_muted, "DEBUG WINDOW");

    const SDL_Color state_color = state.running ? palette.accent : palette.warning;
    const std::string state_label = state.running ? "RUNNING" : "PAUSED";
    const SDL_Rect chip_rect{rect.x + rect.w - 172, rect.y + 18, 152, 28};
    FillRect(renderer, chip_rect, SDL_Color{state_color.r, state_color.g, state_color.b, 50});
    DrawRect(renderer, chip_rect, state_color);
    DrawText(renderer, chip_rect.x + 14, chip_rect.y + 7, 2, state_color, state_label);

    DrawText(renderer,
             rect.x + rect.w - 246,
             rect.y + 54,
             2,
             palette.text_muted,
             "SAMPLES " + std::to_string(sample_count));

    if (metrics.sample_count > 0) {
        DrawText(renderer,
                 rect.x + rect.w - 246,
                 rect.y + 74,
                 2,
                 palette.text_muted,
                 "FRAME " + std::to_string(sample_count));
    }
}

void DrawStatsGrid(SDL_Renderer* renderer,
                   int width,
                   const Palette& palette,
                   const framewatch::OverlaySnapshot& snapshot,
                   const framewatch::MetricsSnapshot& metrics,
                   const WindowState& state,
                   std::size_t sample_count) {
    constexpr int columns = 4;
    constexpr int card_height = 108;
    constexpr int horizontal_gap = 16;
    constexpr int vertical_gap = 16;
    constexpr int top = 132;
    const int grid_width = width - 48;
    const int card_width = (grid_width - ((columns - 1) * horizontal_gap)) / columns;

    std::vector<std::pair<std::string, std::string>> items;
    items.reserve(snapshot.stats.size() + 2);
    for (const auto& stat : snapshot.stats) {
        items.push_back({stat.label, stat.value});
    }
    items.push_back({"Samples", std::to_string(sample_count)});
    items.push_back({"State", state.running ? "RUNNING" : "PAUSED"});

    const std::array<SDL_Color, 8> accents{
        palette.accent,
        palette.accent_2,
        palette.warning,
        palette.danger,
        palette.accent,
        palette.accent_2,
        palette.warning,
        state.running ? palette.accent : palette.warning,
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

        const std::string value =
            (items[i].first == "State" && metrics.sample_count == 0) ? "WARMUP" : items[i].second;

        DrawCard(renderer,
                 rect,
                 palette.panel,
                 palette.panel_border,
                 accents[i % accents.size()],
                 palette.text_muted,
                 palette.text_primary,
                 items[i].first,
                 value);
    }
}

void DrawGraph(SDL_Renderer* renderer,
               const SDL_Rect& rect,
               const Palette& palette,
               const framewatch::OverlaySnapshot& snapshot) {
    FillRect(renderer, rect, palette.panel);
    DrawRect(renderer, rect, palette.panel_border);

    DrawText(renderer, rect.x + 18, rect.y + 16, 2, palette.text_primary, "FRAMETIME GRAPH");
    DrawText(renderer,
             rect.x + rect.w - 132,
             rect.y + 16,
             2,
             palette.text_muted,
             "MS " + FormatDouble(snapshot.graph_min_ms, 1) + " / " +
                 FormatDouble(snapshot.graph_max_ms, 1));

    const SDL_Rect plot_rect{rect.x + 18, rect.y + 52, rect.w - 36, rect.h - 84};
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
    const SDL_Rect dot_rect{latest_x - 3, latest_y - 3, 7, 7};
    FillRect(renderer, dot_rect, palette.accent);
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
             rect.y + 14,
             2,
             palette.text_muted,
             "SPACE PAUSE   R RESET   E EXPORT   ESC QUIT");

    const SDL_Color status_color =
        (state.status_until > SteadyClock::now()) ? palette.accent : palette.text_muted;
    DrawText(renderer,
             rect.x + rect.w - TextWidth(state.status_text, 2) - 16,
             rect.y + 14,
             2,
             status_color,
             state.status_text);
}

bool ExportSession(const SyntheticBenchmark& benchmark, const AppOptions& options) {
    const bool csv_ok = benchmark.logger.ExportCsv(options.csv_path);
    const bool json_ok = benchmark.logger.ExportJson(options.json_path);

    std::cout << "Export CSV: " << (csv_ok ? "ok" : "failed") << " -> "
              << options.csv_path.string() << '\n';
    std::cout << "Export JSON: " << (json_ok ? "ok" : "failed") << " -> "
              << options.json_path.string() << '\n';

    return csv_ok && json_ok;
}

int RunSmokeTest(const AppOptions& options) {
    SyntheticBenchmark benchmark;
    for (int i = 0; i < 240; ++i) {
        benchmark.Step();
    }

    const auto metrics = benchmark.Snapshot();
    const auto overlay = benchmark.OverlaySnapshot();
    const bool exported = ExportSession(benchmark, options);

    std::cout << "Smoke test samples: " << benchmark.logger.Size() << '\n';
    std::cout << "FPS: " << FormatDouble(metrics.current_fps, 2) << '\n';
    std::cout << "Average FPS: " << FormatDouble(metrics.average_fps, 2) << '\n';
    std::cout << "Overlay points: " << overlay.graph.size() << '\n';
    return exported ? EXIT_SUCCESS : EXIT_FAILURE;
}

int RunWindow(const AppOptions& options) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow("FrameWatch Mini Debug Window",
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
    SyntheticBenchmark benchmark;
    WindowState state;

    while (!state.quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                state.quit = true;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
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
                    case SDLK_r:
                        benchmark.Reset();
                        SetStatus(state, "SESSION RESET", std::chrono::seconds(2));
                        break;
                    case SDLK_e:
                        SetStatus(state,
                                  ExportSession(benchmark, options) ? "EXPORT COMPLETE"
                                                                    : "EXPORT FAILED",
                                  std::chrono::seconds(3));
                        break;
                    default:
                        break;
                }
            }
        }

        const SteadyClock::time_point now = SteadyClock::now();
        if (state.running && (now - state.last_step_at) >= std::chrono::milliseconds(16)) {
            benchmark.Step();
            state.last_step_at = now;
        }

        if ((now - state.last_title_update_at) >= std::chrono::milliseconds(250)) {
            UpdateWindowTitle(window, benchmark.Snapshot(), state);
            state.last_title_update_at = now;
        }

        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);

        const auto metrics = benchmark.Snapshot();
        const auto overlay = benchmark.OverlaySnapshot();

        DrawGradientBackground(renderer,
                               width,
                               height,
                               palette.background_top,
                               palette.background_bottom);
        DrawHeader(renderer, width, palette, metrics, state, benchmark.logger.Size());
        DrawStatsGrid(renderer, width, palette, overlay, metrics, state, benchmark.logger.Size());
        DrawGraph(renderer, SDL_Rect{24, 380, width - 48, height - 474}, palette, overlay);
        DrawFooter(renderer, width, height, palette, state);

        SDL_RenderPresent(renderer);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    const AppOptions options = ParseArgs(argc, argv);
    if (options.smoke_test) {
        return RunSmokeTest(options);
    }
    return RunWindow(options);
}
