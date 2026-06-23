#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "framewatch/platform/window_targeting.h"

namespace dw {

using SteadyClock = std::chrono::steady_clock;

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
        if (generated_frames % 120 == 0) frametime_ms += 8.5;
        if (generated_frames % 257 == 0) frametime_ms += 16.0;
        return frametime_ms;
    }
};

struct WindowState {
    bool running{true};
    bool quit{false};
    bool show_settings_panel{false};
    bool editing_target_query{false};
    int target_list_start_index{0};
    int hovered_target_index{-1};
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
    CycleTargetFps,
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

constexpr double kStutterBudgetMultiplier = 1.5;
constexpr int kSettingsPanelMargin = 16;
constexpr int kSettingsMaxVisibleRows = 6;
constexpr int kSettingsButtonGap = 6;
constexpr int kSettingsButtonHeight = 24;
constexpr int kSettingsButtonStartY = 84;
constexpr int kSettingsTargetListHeaderHeight = 22;
constexpr int kSettingsTargetRowStride = 22;
constexpr int kSettingsInfoHeight = 108;
constexpr int kSettingsBottomPadding = 16;

}  // namespace dw
