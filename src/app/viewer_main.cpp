#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "debug_window/bitmap_font.h"
#include "debug_window/renderer.h"
#include "debug_window/types.h"
#include "framewatch/gpu/gpu_metrics_sampler.h"
#include "framewatch/ipc/ipc_server.h"
#include "framewatch/session/performance_session.h"

namespace {

constexpr int kWinW  = 640;
constexpr int kWinH  = 420;
constexpr int kPad   = 16;

enum class ViewerState { Waiting, Live, Disconnected };

// ── Colours ───────────────────────────────────────────────────────────────────

const dw::Palette kPal{};

SDL_Color Fade(SDL_Color c, Uint8 a) { c.a = a; return c; }

// ── Drawing helpers ───────────────────────────────────────────────────────────

void DrawStatCard(SDL_Renderer* r, int x, int y, int w, int h,
                  const char* label, const char* value,
                  SDL_Color value_color) {
    const SDL_Rect bg{x, y, w, h};
    dw::FillRect(r, bg, kPal.panel);
    dw::DrawRect(r, bg, kPal.panel_border);
    dw::DrawText(r, x + 8, y + 7,  1, kPal.text_muted,    label);
    dw::DrawText(r, x + 8, y + 20, 2, value_color,         value);
}

// Horizontal progress bar — bg then filled portion.
void DrawBar(SDL_Renderer* r, int x, int y, int w, int h,
             float fraction, SDL_Color fill) {
    static constexpr SDL_Color kBg{55, 65, 81, 180};
    const SDL_Rect bg_rect{x, y, w, h};
    dw::FillRect(r, bg_rect, kBg);
    const int fill_w = static_cast<int>(std::clamp(fraction, 0.f, 1.f) * w);
    if (fill_w > 0) {
        const SDL_Rect fill_rect{x, y, fill_w, h};
        dw::FillRect(r, fill_rect, fill);
    }
}

void DrawGraph(SDL_Renderer* r, const SDL_Rect& rect,
               const framewatch::OverlaySnapshot& snap,
               double target_ft_ms) {
    dw::FillRect(r, rect, kPal.panel);
    dw::DrawRect(r, rect, kPal.panel_border);

    if (snap.graph.empty()) return;

    const double lo = snap.graph_min_ms;
    const double hi = std::max(snap.graph_max_ms, target_ft_ms * 1.5);
    const double range = (hi > lo) ? (hi - lo) : 1.0;
    const int    n    = static_cast<int>(snap.graph.size());

    auto mapY = [&](double ft_ms) -> int {
        double t = 1.0 - std::clamp((ft_ms - lo) / range, 0.0, 1.0);
        return rect.y + static_cast<int>(t * (rect.h - 2)) + 1;
    };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── FPS reference grid lines (120 / 60 / 30 fps) ─────────────────────────
    struct RefLine { double ft_ms; const char* label; };
    static constexpr RefLine kRefs[] = {
        { 8.333, "120"},
        {16.667, " 60"},
        {33.333, " 30"},
    };
    for (const auto& ref : kRefs) {
        const int gy = mapY(ref.ft_ms);
        if (gy <= rect.y || gy >= rect.y + rect.h) continue;
        SDL_SetRenderDrawColor(r, kPal.grid.r, kPal.grid.g, kPal.grid.b, 80);
        SDL_RenderDrawLine(r, rect.x + 1, gy, rect.x + rect.w - 2, gy);
        dw::DrawText(r, rect.x + rect.w - 22, gy - 7, 1, kPal.grid, ref.label);
    }

    // ── Budget reference line ─────────────────────────────────────────────────
    const int budget_y = mapY(target_ft_ms);
    SDL_SetRenderDrawColor(r, kPal.accent.r, kPal.accent.g, kPal.accent.b, 80);
    SDL_RenderDrawLine(r, rect.x + 1, budget_y, rect.x + rect.w - 2, budget_y);

    // ── Filled area under curve ───────────────────────────────────────────────
    const int bottom = rect.y + rect.h - 1;
    for (int i = 0; i < n; ++i) {
        const double ft    = snap.graph[i].frametime_ms;
        const int    gx    = rect.x + 1 + i * (rect.w - 2) / std::max(n - 1, 1);
        const int    gy    = mapY(ft);
        const bool   spike = ft > target_ft_ms * 1.5;
        SDL_Color fc = spike ? kPal.danger : kPal.accent_2;
        SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, 28);
        SDL_RenderDrawLine(r, gx, gy, gx, bottom);
    }

    // ── Frametime polyline ────────────────────────────────────────────────────
    for (int i = 1; i < n; ++i) {
        const double ft_prev = snap.graph[i - 1].frametime_ms;
        const double ft_curr = snap.graph[i].frametime_ms;

        const int x0 = rect.x + 1 + (i - 1) * (rect.w - 2) / std::max(n - 1, 1);
        const int x1 = rect.x + 1 + i       * (rect.w - 2) / std::max(n - 1, 1);
        const int y0 = mapY(ft_prev);
        const int y1 = mapY(ft_curr);

        const bool spike = ft_curr > target_ft_ms * 1.5;
        SDL_Color lc = spike ? kPal.danger : kPal.accent_2;
        SDL_SetRenderDrawColor(r, lc.r, lc.g, lc.b, 255);
        SDL_RenderDrawLine(r, x0, y0, x1, y1);
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

}  // namespace

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "FrameWatch Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWinW, kWinH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!window || !renderer) {
        std::cerr << "SDL: " << SDL_GetError() << '\n';
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // ── App state ─────────────────────────────────────────────────────────────
    framewatch::PerformanceSession session(360, 5000);
    framewatch::IpcServer          ipc;
    framewatch::GpuMetricsSampler  gpu_sampler;
    gpu_sampler.Start();
    ViewerState                    state = ViewerState::Waiting;
    bool                           was_connected = false;
    std::string                    status_msg    = "Waiting for game...";
    auto                           status_until  = dw::SteadyClock::now();
    constexpr double               kTargetFps    = 60.0;
    const double                   kTargetFtMs   = 1000.0 / kTargetFps;

    const std::filesystem::path kExportCsv  = "/tmp/framewatch_viewer.csv";
    const std::filesystem::path kExportJson = "/tmp/framewatch_viewer.json";

    if (!ipc.Start()) {
        std::cerr << "IPC: failed to start — endpoint in use?\n";
    } else {
        std::cout << "IPC: listening on " << framewatch::IpcServer::kEndpoint << '\n';
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; }
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q:
                    running = false;
                    break;
                case SDLK_r:
                    session.Reset();
                    state = ipc.HasClient() ? ViewerState::Live : ViewerState::Waiting;
                    status_msg   = "Session reset";
                    status_until = dw::SteadyClock::now() + std::chrono::seconds(2);
                    break;
                case SDLK_e:
                    if (session.ExportPreferred(kExportCsv, kExportJson)) {
                        status_msg   = "Exported to /tmp/";
                        status_until = dw::SteadyClock::now() + std::chrono::seconds(3);
                    }
                    break;
                default: break;
                }
            }
        }

        // IPC connection transitions
        const bool connected = ipc.HasClient();
        if (connected && !was_connected) {
            state        = ViewerState::Live;
            status_msg   = "Game connected";
            status_until = dw::SteadyClock::now() + std::chrono::seconds(2);
        } else if (!connected && was_connected) {
            state        = ViewerState::Disconnected;
            status_msg   = "Game disconnected";
            status_until = dw::SteadyClock::now() + std::chrono::seconds(4);
        }
        was_connected = connected;

        // Drain IPC samples
        for (const auto& s : ipc.DrainSamples()) {
            session.CaptureSyntheticFrame(s.frametime_ms);
        }

        // ── Render ────────────────────────────────────────────────────────────
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        dw::DrawGradientBackground(renderer, kWinW, kWinH,
                                   kPal.background_top, kPal.background_bottom);

        const auto snap    = session.LiveMetrics();
        const auto gsnap   = session.GraphSnapshot();
        const auto now     = dw::SteadyClock::now();

        // ── Window title (live FPS) ───────────────────────────────────────────
        {
            std::string title = "FrameWatch";
            if (snap.sample_count > 0) {
                title += "  \xe2\x80\x94  " + dw::FormatDouble(snap.current_fps, 1) + " fps";
            }
            SDL_SetWindowTitle(window, title.c_str());
        }

        // ── Header ────────────────────────────────────────────────────────────
        dw::DrawText(renderer, kPad, kPad + 2, 2, kPal.text_primary, "FRAMEWATCH");

        // State indicator (small, top-right)
        const char* state_str  = "WAITING";
        SDL_Color   state_col  = kPal.text_muted;
        if (state == ViewerState::Live) {
            state_str = "LIVE";
            state_col = kPal.accent;
        } else if (state == ViewerState::Disconnected) {
            state_str = "DISCONNECTED";
            state_col = kPal.warning;
        }
        dw::DrawText(renderer, kWinW - kPad - static_cast<int>(strlen(state_str)) * 6,
                     kPad + 6, 1, state_col, state_str);

        // Live FPS — large number, top-right area below state
        if (snap.sample_count > 0) {
            const std::string fps_live = dw::FormatDouble(snap.current_fps, 0);
            const int fw = static_cast<int>(fps_live.size()) * 18; // scale=3, 6*3 per char
            const SDL_Color fps_col = snap.current_fps >= kTargetFps * 0.97
                                          ? kPal.accent : kPal.warning;
            dw::DrawText(renderer, kWinW - kPad - fw, kPad - 2, 3, fps_col, fps_live.c_str());
            dw::DrawText(renderer, kWinW - kPad - fw - 24, kPad + 18, 1, kPal.text_muted, "fps");
        }

        // Divider
        SDL_SetRenderDrawColor(renderer, kPal.panel_border.r, kPal.panel_border.g,
                               kPal.panel_border.b, 160);
        SDL_RenderDrawLine(renderer, kPad, kPad + 22, kWinW - kPad, kPad + 22);

        // ── Stat cards ────────────────────────────────────────────────────────
        const int cardY   = kPad + 30;
        const int cardH   = 46;
        const int cardW   = (kWinW - kPad * 2 - 12) / 4;
        const int cardGap = 4;

        const std::string fps_str  = dw::FormatDouble(snap.average_fps,            1);
        const std::string ft_str   = dw::FormatDouble(snap.average_frametime_ms,   1) + "ms";
        const std::string p1_str   = dw::FormatDouble(snap.one_percent_low_fps,    1);
        const std::string p01_str  = dw::FormatDouble(snap.point_one_percent_low_fps, 1);

        const SDL_Color green  = kPal.accent;
        const SDL_Color normal = kPal.text_primary;

        DrawStatCard(renderer, kPad,                             cardY, cardW, cardH,
                     "AVG FPS",  fps_str.c_str(),  snap.average_fps > 58 ? green : normal);
        DrawStatCard(renderer, kPad + cardW + cardGap,           cardY, cardW, cardH,
                     "AVG FT",   ft_str.c_str(),   normal);
        DrawStatCard(renderer, kPad + (cardW + cardGap) * 2,     cardY, cardW, cardH,
                     "1% LOW",   p1_str.c_str(),   snap.one_percent_low_fps > 50 ? green : kPal.warning);
        DrawStatCard(renderer, kPad + (cardW + cardGap) * 3,     cardY, cardW, cardH,
                     "0.1% LOW", p01_str.c_str(),  snap.point_one_percent_low_fps > 40 ? green : kPal.warning);

        // ── GPU row ───────────────────────────────────────────────────────────
        const framewatch::GpuMetrics gpu     = gpu_sampler.LastSample();
        const int                    gpuRowY = cardY + cardH + 6;

        if (gpu.available) {
            const std::string load_s = dw::FormatDouble(gpu.gpu_load_percent, 0) + "%";
            const std::string temp_s = dw::FormatDouble(gpu.gpu_temp_c, 0) + "\xB0""C";
            const uint64_t vram_mb   = gpu.vram_used_bytes  / (1024 * 1024);
            const uint64_t vtot_mb   = gpu.vram_total_bytes / (1024 * 1024);
            const std::string vram_s = std::to_string(vram_mb) + "/" +
                                       std::to_string(vtot_mb) + "MB";

            // Text line: name + load + temp + vram
            const std::string gpu_line = gpu.gpu_name + "  GPU:" + load_s +
                                         "  TEMP:" + temp_s + "  VRAM:" + vram_s;
            dw::DrawText(renderer, kPad, gpuRowY, 1, kPal.text_muted, gpu_line.c_str());

            // GPU load bar
            const SDL_Color load_col = gpu.gpu_load_percent < 70.f ? kPal.accent
                                     : gpu.gpu_load_percent < 90.f ? kPal.warning
                                     :                               kPal.danger;
            DrawBar(renderer, kPad, gpuRowY + 12, 180, 5,
                    gpu.gpu_load_percent / 100.f, load_col);

            // VRAM bar
            const float vram_frac = vtot_mb > 0
                                        ? static_cast<float>(vram_mb) / vtot_mb
                                        : 0.f;
            DrawBar(renderer, kPad + 188, gpuRowY + 12, 120, 5,
                    vram_frac, kPal.accent_2);
        } else {
            dw::DrawText(renderer, kPad, gpuRowY, 1, kPal.text_muted,
                         (std::string("GPU: ") + gpu_sampler.ProviderName()).c_str());
        }

        // ── Graph ─────────────────────────────────────────────────────────────
        const int graphY = cardY + cardH + 32;   // +12 extra for GPU bars
        const int graphH = kWinH - graphY - kPad * 3 - 24;
        const SDL_Rect graph_rect{kPad, graphY, kWinW - kPad * 2, graphH};
        DrawGraph(renderer, graph_rect, gsnap, kTargetFtMs);

        // min/avg/max labels on graph
        const std::string min_s = dw::FormatDouble(snap.min_frametime_ms, 1) + "ms";
        const std::string avg_s = dw::FormatDouble(snap.average_frametime_ms, 1) + "ms";
        const std::string max_s = dw::FormatDouble(snap.max_frametime_ms, 1) + "ms";
        dw::DrawText(renderer, kPad + 4, graphY + 4,              1, Fade(kPal.text_muted, 200), max_s.c_str());
        dw::DrawText(renderer, kPad + 4, graphY + graphH / 2 - 4, 1, Fade(kPal.text_muted, 200), avg_s.c_str());
        dw::DrawText(renderer, kPad + 4, graphY + graphH - 12,    1, Fade(kPal.text_muted, 200), min_s.c_str());

        // ── Status / footer ───────────────────────────────────────────────────
        const int footerY = kWinH - kPad - 10;

        // Status flash
        if (now < status_until) {
            dw::DrawText(renderer, kPad, footerY - 14, 1, kPal.accent, status_msg.c_str());
        }

        // Endpoint + frame count
        std::string endpoint_str = std::string(framewatch::IpcServer::kEndpoint);
        dw::DrawText(renderer, kPad, footerY, 1, kPal.text_muted, endpoint_str.c_str());

        const std::string frame_str =
            "FRAMES: " + std::to_string(snap.sample_count) +
            "  [R]eset  [E]xport  [Q]uit";
        const int fw = static_cast<int>(frame_str.size()) * 6;
        dw::DrawText(renderer, kWinW - kPad - fw, footerY, 1, kPal.text_muted, frame_str.c_str());

        SDL_RenderPresent(renderer);
    }

    gpu_sampler.Stop();
    ipc.Stop();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
