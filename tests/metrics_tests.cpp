#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "framewatch/core/frametime_tracker.h"
#include "framewatch/core/metrics_engine.h"
#include "framewatch/exporter/csv_exporter.h"
#include "framewatch/exporter/json_exporter.h"
#include "framewatch/hooks/hook_overlay_service.h"
#include "framewatch/overlay/overlay_runtime.h"
#include "framewatch/overlay/overlay_settings.h"
#include "framewatch/session/performance_session.h"

namespace {

framewatch::FrameSample MakeSample(std::uint64_t frame_index,
                                   double timestamp_seconds,
                                   double frametime_ms) {
    framewatch::FrameSample sample;
    sample.frame_index = frame_index;
    sample.timestamp_seconds = timestamp_seconds;
    sample.frametime_ms = frametime_ms;
    sample.fps = 1'000.0 / frametime_ms;
    return sample;
}

class RecordingOverlayRenderer final : public framewatch::OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "RecordingOverlayRenderer"; }

    std::string_view Description() const noexcept override {
        return "Recording overlay renderer for tests";
    }

    bool Initialize() override {
        initialized = true;
        return true;
    }

    framewatch::OverlayRenderActions Render(const framewatch::OverlaySnapshot& snapshot,
                                            const framewatch::PresentEvent& present_event) override {
        ++render_calls;
        last_snapshot = snapshot;
        last_present = present_event;
        if (render_calls <= static_cast<int>(actions_by_render.size())) {
            return actions_by_render[static_cast<std::size_t>(render_calls - 1)];
        }
        return {};
    }

    void Shutdown() noexcept override { initialized = false; }

    bool initialized{false};
    int render_calls{0};
    framewatch::OverlaySnapshot last_snapshot;
    framewatch::PresentEvent last_present;
    std::vector<framewatch::OverlayRenderActions> actions_by_render;
};

class RecordingPresentHook final : public framewatch::PresentHook {
  public:
    framewatch::HookBackend Backend() const noexcept override {
        return framewatch::HookBackend::Dx11;
    }

    framewatch::HookState State() const noexcept override { return state_; }

    std::string_view Description() const noexcept override {
        return "RecordingPresentHook for tests";
    }

    void SetPresentCallback(framewatch::PresentCallback callback) override {
        callback_ = std::move(callback);
        callback_was_set = true;
    }

    bool Install() override {
        state_ = install_result ? framewatch::HookState::Running : framewatch::HookState::Error;
        install_was_called = true;
        return install_result;
    }

    void Remove() noexcept override {
        remove_was_called = true;
        state_ = framewatch::HookState::Ready;
    }

    void Emit(const framewatch::PresentEvent& present_event) {
        if (callback_) callback_(present_event);
    }

    void Emit(framewatch::FrameClock::time_point timestamp) {
        framewatch::PresentEvent present_event;
        present_event.timestamp = timestamp;
        Emit(present_event);
    }

    bool install_result{true};
    bool callback_was_set{false};
    bool install_was_called{false};
    bool remove_was_called{false};

  private:
    framewatch::HookState state_{framewatch::HookState::Ready};
    framewatch::PresentCallback callback_;
};

}  // namespace

using Catch::Approx;

// ---------------------------------------------------------------------------
// FrametimeTracker
// ---------------------------------------------------------------------------

TEST_CASE("FrametimeTracker captures frametime delta", "[frametime]") {
    framewatch::FrametimeTracker tracker;
    auto timestamp = framewatch::FrameClock::time_point{};

    const auto first = tracker.Capture(timestamp);
    timestamp += std::chrono::milliseconds(16);
    const auto second = tracker.Capture(timestamp);

    CHECK_FALSE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(second->frametime_ms == Approx(16.0).margin(0.001));
    CHECK(tracker.FrameCount() == 1);
}

// ---------------------------------------------------------------------------
// MetricsEngine
// ---------------------------------------------------------------------------

TEST_CASE("MetricsEngine stable frametimes", "[metrics]") {
    framewatch::MetricsEngine metrics(256);
    for (std::uint64_t i = 1; i <= 120; ++i) {
        metrics.PushSample(MakeSample(i, static_cast<double>(i) / 60.0, 16.666667));
    }
    const framewatch::MetricsSnapshot snapshot = metrics.Snapshot();

    CHECK(snapshot.sample_count == 120);
    CHECK(snapshot.average_fps == Approx(60.0).margin(0.05));
    CHECK(snapshot.current_fps == Approx(60.0).margin(0.05));
    CHECK(snapshot.one_percent_low_fps == Approx(60.0).margin(0.05));
    CHECK(snapshot.point_one_percent_low_fps == Approx(60.0).margin(0.05));
    CHECK(snapshot.frametime_variance_ms2 == Approx(0.0).margin(0.0001));
}

TEST_CASE("MetricsEngine rolling history limit", "[metrics]") {
    framewatch::MetricsEngine metrics(5);
    for (std::uint64_t i = 1; i <= 10; ++i) {
        metrics.PushSample(MakeSample(i, static_cast<double>(i), 10.0 + static_cast<double>(i)));
    }
    const framewatch::MetricsSnapshot snapshot = metrics.Snapshot();
    const std::vector<double> history = metrics.RecentFrametimeHistory();

    CHECK(snapshot.sample_count == 5);
    CHECK(history.size() == 5);
    CHECK(history.front() == Approx(16.0).margin(0.001));
    CHECK(history.back() == Approx(20.0).margin(0.001));
    CHECK(snapshot.latest_frametime_ms == Approx(20.0).margin(0.001));
}

TEST_CASE("MetricsEngine variance correctness", "[metrics][variance]") {
    // {10, 20, 30, 40}: mean=25, population variance=125
    framewatch::MetricsEngine metrics(10);
    for (double ft : {10.0, 20.0, 30.0, 40.0}) {
        framewatch::FrameSample s;
        s.frametime_ms = ft;
        s.fps = 1000.0 / ft;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();

    CHECK(snap.frametime_variance_ms2 == Approx(125.0).margin(0.001));
    CHECK(snap.average_frametime_ms == Approx(25.0).margin(0.001));
    CHECK(snap.average_fps == Approx(1000.0 / 25.0).margin(0.001));
}

TEST_CASE("MetricsEngine variance after rolling eviction", "[metrics][variance]") {
    // Window=3, push {10,20,30,40}. Window holds {20,30,40}: mean=30, variance=200/3
    framewatch::MetricsEngine metrics(3);
    for (double ft : {10.0, 20.0, 30.0, 40.0}) {
        framewatch::FrameSample s;
        s.frametime_ms = ft;
        s.fps = 1000.0 / ft;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();

    CHECK(snap.average_frametime_ms == Approx(30.0).margin(0.001));
    CHECK(snap.frametime_variance_ms2 == Approx(200.0 / 3.0).margin(0.001));
    CHECK(snap.average_fps == Approx(1000.0 / snap.average_frametime_ms).margin(0.001));
}

TEST_CASE("MetricsEngine variance never negative for constant frametimes", "[metrics][variance]") {
    framewatch::MetricsEngine metrics(256);
    for (int i = 0; i < 500; ++i) {
        framewatch::FrameSample s;
        s.frametime_ms = 16.666667;
        s.fps = 60.0;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();

    CHECK(snap.frametime_variance_ms2 >= 0.0);
    CHECK(snap.frametime_variance_ms2 == Approx(0.0).margin(1e-6));
}

// ---------------------------------------------------------------------------
// Exporter
// ---------------------------------------------------------------------------

TEST_CASE("CSV and JSON exporter round-trip", "[exporter]") {
    std::vector<framewatch::FrameSample> samples;
    for (std::uint64_t i = 1; i <= 5; ++i) {
        framewatch::FrameSample s;
        s.frame_index = i;
        s.timestamp_seconds = static_cast<double>(i) / 60.0;
        s.frametime_ms = 16.666667;
        s.fps = 60.0;
        samples.push_back(s);
    }

    const auto tmp = std::filesystem::temp_directory_path();
    const auto csv_path = tmp / "framewatch_exporter_test.csv";
    const auto json_path = tmp / "framewatch_exporter_test.json";
    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);

    REQUIRE(framewatch::ExportSamplesToCsv(samples, csv_path));
    REQUIRE(std::filesystem::exists(csv_path));
    REQUIRE(framewatch::ExportSamplesToJson(samples, json_path));
    REQUIRE(std::filesystem::exists(json_path));

    {
        std::ifstream csv_file(csv_path);
        REQUIRE(csv_file.is_open());
        std::string line;
        int line_count = 0;
        while (std::getline(csv_file, line)) { ++line_count; }
        CHECK(line_count == 6);  // 1 header + 5 data rows
    }  // csv_file closed here before remove (required on Windows)

    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
}

// ---------------------------------------------------------------------------
// PerformanceSession
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceSession benchmark lifecycle", "[session]") {
    framewatch::PerformanceSession session(256, 256);
    session.ResetSyntheticTimeline();

    for (int i = 0; i < 30; ++i) {
        session.CaptureSyntheticFrame(16.666667);
    }

    session.StartBenchmark();
    for (int i = 0; i < 90; ++i) {
        session.CaptureSyntheticFrame(16.666667);
    }

    const framewatch::BenchmarkSummary active = session.CurrentBenchmark();
    CHECK(active.active);
    CHECK(active.has_data);
    CHECK(active.frame_count == 90);
    CHECK(active.duration_seconds == Approx(1.5).margin(0.02));

    session.StopBenchmark();
    const framewatch::BenchmarkSummary stopped = session.CurrentBenchmark();
    CHECK_FALSE(stopped.active);
    CHECK(stopped.frame_count == 90);
    CHECK(stopped.metrics.average_fps == Approx(60.0).margin(0.05));
}

// ---------------------------------------------------------------------------
// OverlayRuntime
// ---------------------------------------------------------------------------

TEST_CASE("OverlayRuntime present flow", "[overlay]") {
    auto renderer = std::make_unique<RecordingOverlayRenderer>();
    RecordingOverlayRenderer* renderer_ptr = renderer.get();
    framewatch::OverlayRuntime runtime(std::move(renderer), 128, 128);

    REQUIRE(runtime.Initialize());

    constexpr std::uintptr_t kSwapChainTag = 0x1234;
    auto timestamp = framewatch::FrameClock::time_point{};
    framewatch::PresentEvent first_present;
    first_present.api = framewatch::GraphicsApi::Dx11;
    first_present.timestamp = timestamp;
    first_present.native_swap_chain = reinterpret_cast<void*>(kSwapChainTag);
    first_present.sync_interval = 1;

    CHECK_FALSE(runtime.OnPresent(first_present));

    framewatch::PresentEvent second_present = first_present;
    second_present.timestamp += std::chrono::milliseconds(16);

    REQUIRE(runtime.OnPresent(second_present));
    CHECK(renderer_ptr->render_calls == 1);
    CHECK(runtime.LastSnapshot() != nullptr);
    CHECK(runtime.Session().LiveSampleCount() == 1);
    CHECK(renderer_ptr->last_present.native_swap_chain == reinterpret_cast<void*>(kSwapChainTag));
    CHECK(renderer_ptr->last_snapshot.graph_label == std::string("LIVE GRAPH"));

    runtime.StartBenchmark();
    framewatch::PresentEvent third_present = second_present;
    third_present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(third_present);
    const framewatch::BenchmarkSummary active = runtime.Session().CurrentBenchmark();
    CHECK(active.active);
    CHECK(active.frame_count == 1);

    runtime.StopBenchmark();
    const framewatch::BenchmarkSummary stopped = runtime.Session().CurrentBenchmark();
    CHECK_FALSE(stopped.active);
    CHECK(stopped.frame_count == 1);

    runtime.Shutdown();
    CHECK_FALSE(runtime.IsInitialized());
    CHECK_FALSE(renderer_ptr->initialized);
}

TEST_CASE("OverlayRuntime renderer actions", "[overlay]") {
    auto renderer = std::make_unique<RecordingOverlayRenderer>();
    RecordingOverlayRenderer* renderer_ptr = renderer.get();
    renderer_ptr->actions_by_render = {
        framewatch::OverlayRenderActions{.toggle_benchmark = true},
        framewatch::OverlayRenderActions{.toggle_benchmark = true, .export_requested = true},
        framewatch::OverlayRenderActions{.reset_session = true},
    };

    framewatch::OverlayRuntime runtime(std::move(renderer), 128, 128);
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto csv_path = temp_dir / "framewatch_overlay_runtime_actions.csv";
    const auto json_path = temp_dir / "framewatch_overlay_runtime_actions.json";
    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    runtime.SetExportPaths(csv_path, json_path);

    REQUIRE(runtime.Initialize());

    auto timestamp = framewatch::FrameClock::time_point{};
    framewatch::PresentEvent present;
    present.api = framewatch::GraphicsApi::Dx11;
    present.native_swap_chain = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xD00D));
    present.timestamp = timestamp;
    runtime.OnPresent(present);

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    CHECK(runtime.Session().IsBenchmarkRecording());

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    CHECK_FALSE(runtime.Session().IsBenchmarkRecording());
    CHECK(renderer_ptr->last_snapshot.status_text == std::string("BENCHMARK START"));

    const auto benchmark = runtime.Session().CurrentBenchmark();
    CHECK(benchmark.frame_count == 1);
    CHECK(std::filesystem::exists(csv_path));
    CHECK(std::filesystem::exists(json_path));

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    CHECK(renderer_ptr->last_snapshot.status_text == std::string("EXPORT OK"));
    CHECK(runtime.Session().LiveSampleCount() == 0);
    CHECK(runtime.LastSnapshot() == nullptr);
    CHECK_FALSE(runtime.Session().IsBenchmarkRecording());

    present.timestamp += std::chrono::milliseconds(16);
    CHECK_FALSE(runtime.OnPresent(present));
    present.timestamp += std::chrono::milliseconds(16);
    CHECK(runtime.OnPresent(present));
    CHECK(renderer_ptr->last_snapshot.status_text == std::string("SESSION RESET"));

    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    runtime.Shutdown();
}

// ---------------------------------------------------------------------------
// OverlaySettings
// ---------------------------------------------------------------------------

TEST_CASE("OverlaySettings opacity controls", "[settings]") {
    framewatch::OverlaySettings settings;

    CHECK(framewatch::ClampOverlayOpacity(0.15) == Approx(0.35).margin(0.0001));
    CHECK(framewatch::ClampOverlayOpacity(1.5) == Approx(1.0).margin(0.0001));

    framewatch::AdjustOverlayOpacity(settings, -0.75);
    CHECK(settings.panel_opacity == Approx(0.35).margin(0.0001));
}

TEST_CASE("OverlaySettings target FPS controls", "[settings]") {
    CHECK(framewatch::ClampTargetFps(5) == 10);
    CHECK(framewatch::ClampTargetFps(5'000) == 1'000);
    CHECK(framewatch::CycleTargetFps(60, 1) == 90);
    CHECK(framewatch::CycleTargetFps(60, -1) == 30);
    CHECK(framewatch::CycleTargetFps(58, 1) == 90);
    CHECK(framewatch::CycleTargetFps(240, 1) == 30);
}

TEST_CASE("OverlaySettings dock anchor controls", "[settings]") {
    framewatch::OverlaySettings settings;
    settings.dock_anchor = framewatch::OverlayDockAnchor::LeftBottom;
    settings.dock_anchor = framewatch::CycleOverlayDockAnchor(settings.dock_anchor);

    CHECK(settings.dock_anchor == framewatch::OverlayDockAnchor::RightTop);
    CHECK(std::string(framewatch::OverlayDockAnchorName(framewatch::OverlayDockAnchor::RightBottom)) ==
          std::string("RIGHT BOTTOM"));
}

TEST_CASE("OverlaySettings JSON persistence", "[settings]") {
    framewatch::OverlaySettings settings;
    settings.show_overlay = false;
    settings.show_graph = false;
    settings.show_sidebar = false;
    settings.show_hotkey_hints = false;
    settings.show_settings_panel = true;
    settings.capture_input_when_panel_open = true;
    settings.compact_mode = true;
    settings.panel_opacity = 0.55;
    settings.target_fps = 144;
    settings.dock_anchor = framewatch::OverlayDockAnchor::LeftTop;
    settings.follow_target_window = true;
    settings.target_window_query = "Game \"Window\" \\\\ DX11";
    settings.window_width = 1320;
    settings.window_height = 840;
    settings.window_x = 144;
    settings.window_y = 96;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "framewatch_overlay_settings_test.json";
    std::filesystem::remove(path);

    REQUIRE(framewatch::SaveOverlaySettings(settings, path));

    const auto loaded = framewatch::LoadOverlaySettings(path);
    REQUIRE(loaded.has_value());

    CHECK_FALSE(loaded->show_overlay);
    CHECK_FALSE(loaded->show_graph);
    CHECK_FALSE(loaded->show_sidebar);
    CHECK_FALSE(loaded->show_hotkey_hints);
    CHECK(loaded->show_settings_panel);
    CHECK(loaded->capture_input_when_panel_open);
    CHECK(loaded->compact_mode);
    CHECK(loaded->panel_opacity == Approx(0.55).margin(0.0001));
    CHECK(loaded->target_fps == 144);
    CHECK(loaded->dock_anchor == framewatch::OverlayDockAnchor::LeftTop);
    CHECK(loaded->follow_target_window);
    CHECK(loaded->target_window_query == std::string("Game \"Window\" \\\\ DX11"));
    CHECK(loaded->window_width == 1320);
    CHECK(loaded->window_height == 840);
    CHECK(loaded->window_x == std::optional<int>(144));
    CHECK(loaded->window_y == std::optional<int>(96));

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// HookOverlayService
// ---------------------------------------------------------------------------

TEST_CASE("HookOverlayService wiring", "[hook]") {
    auto hook = std::make_unique<RecordingPresentHook>();
    RecordingPresentHook* hook_ptr = hook.get();
    auto renderer = std::make_unique<RecordingOverlayRenderer>();
    RecordingOverlayRenderer* renderer_ptr = renderer.get();

    framewatch::HookOverlayService service(std::move(hook), std::move(renderer), 128, 128);

    REQUIRE(service.Initialize());
    CHECK(service.IsInitialized());
    CHECK(hook_ptr->callback_was_set);
    CHECK(hook_ptr->install_was_called);

    constexpr std::uintptr_t kSwapChainTag = 0xCAFE;
    auto timestamp = framewatch::FrameClock::time_point{};
    framewatch::PresentEvent first_present;
    first_present.api = framewatch::GraphicsApi::Dx11;
    first_present.timestamp = timestamp;
    first_present.native_swap_chain = reinterpret_cast<void*>(kSwapChainTag);

    framewatch::PresentEvent second_present = first_present;
    second_present.timestamp += std::chrono::milliseconds(16);

    hook_ptr->Emit(first_present);
    hook_ptr->Emit(second_present);

    CHECK(renderer_ptr->render_calls == 1);
    CHECK(service.Runtime().Session().LiveSampleCount() == 1);
    CHECK(renderer_ptr->last_present.native_swap_chain == reinterpret_cast<void*>(kSwapChainTag));

    service.Shutdown();
    CHECK(hook_ptr->remove_was_called);
    CHECK_FALSE(service.IsInitialized());
}
