#pragma once
#include "types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "framewatch/overlay/overlay_settings.h"
#include "framewatch/session/performance_session.h"

namespace dw {

int ComputeSettingsVisibleRows(int height);
int ClampTargetListStartIndex(const TargetingState& targeting, int visible_rows, int start_index);

SettingsPanelLayout BuildSettingsPanelLayout(int width,
                                             int height,
                                             const framewatch::OverlaySettings& settings,
                                             const TargetingState& targeting,
                                             int target_list_start_index);

void DrawHeader(SDL_Renderer* renderer,
                int width,
                const Palette& palette,
                const WindowState& state,
                const framewatch::BenchmarkSummary& benchmark,
                const TargetingState& targeting,
                double over_budget_ratio,
                double stutter_pulse);

void DrawStatsGrid(SDL_Renderer* renderer,
                   int width,
                   const Palette& palette,
                   const framewatch::MetricsSnapshot& live_metrics,
                   const framewatch::BenchmarkSummary& benchmark,
                   const TargetingState& targeting,
                   int target_fps);

void DrawGraph(SDL_Renderer* renderer,
               const SDL_Rect& rect,
               const Palette& palette,
               const framewatch::OverlaySnapshot& snapshot,
               std::string_view label,
               int target_fps,
               double stutter_pulse);

void DrawInfoPanel(SDL_Renderer* renderer,
                   const SDL_Rect& rect,
                   const Palette& palette,
                   SDL_Color accent,
                   std::string_view title,
                   const std::vector<std::string>& lines);

void DrawSidebar(SDL_Renderer* renderer,
                 const SDL_Rect& rect,
                 const Palette& palette,
                 const framewatch::BenchmarkSummary& benchmark,
                 const TargetingState& targeting,
                 const framewatch::OverlaySettings& settings);

void DrawFooter(SDL_Renderer* renderer,
                int width,
                int height,
                const Palette& palette,
                const WindowState& state);

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
                         int window_y);

}  // namespace dw
