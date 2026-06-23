#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "framewatch/core/frametime_tracker.h"
#include "framewatch/core/metrics_engine.h"
#include "framewatch/exporter/csv_exporter.h"
#include "framewatch/exporter/json_exporter.h"
#include "framewatch/hooks/hook_overlay_service.h"
#include "framewatch/overlay/overlay_settings.h"
#include "framewatch/overlay/overlay_runtime.h"
#include "framewatch/session/performance_session.h"

namespace {

bool Expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool ExpectNear(double actual, double expected, double tolerance, std::string_view message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual
                  << ")\n";
        return false;
    }
    return true;
}

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
        if (callback_) {
            callback_(present_event);
        }
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

bool TestFrametimeTracker() {
    framewatch::FrametimeTracker tracker;
    auto timestamp = framewatch::FrameClock::time_point{};

    const auto first = tracker.Capture(timestamp);
    timestamp += std::chrono::milliseconds(16);
    const auto second = tracker.Capture(timestamp);

    bool ok = true;
    ok &= Expect(!first.has_value(), "first capture should not produce a frametime sample");
    ok &= Expect(second.has_value(), "second capture should produce a frametime sample");
    ok &= ExpectNear(second->frametime_ms, 16.0, 0.001, "frametime tracker should measure delta");
    ok &= Expect(tracker.FrameCount() == 1, "tracker should count measured frames");
    return ok;
}

bool TestStableMetrics() {
    framewatch::MetricsEngine metrics(256);

    for (std::uint64_t i = 1; i <= 120; ++i) {
        metrics.PushSample(MakeSample(i, static_cast<double>(i) / 60.0, 16.666667));
    }

    const framewatch::MetricsSnapshot snapshot = metrics.Snapshot();

    bool ok = true;
    ok &= Expect(snapshot.sample_count == 120, "metrics should store all samples within history");
    ok &= ExpectNear(snapshot.average_fps, 60.0, 0.05, "average fps should be close to 60");
    ok &= ExpectNear(snapshot.current_fps, 60.0, 0.05, "current fps should be close to 60");
    ok &= ExpectNear(snapshot.one_percent_low_fps, 60.0, 0.05, "1% low should stay stable");
    ok &= ExpectNear(snapshot.point_one_percent_low_fps,
                     60.0,
                     0.05,
                     "0.1% low should stay stable");
    ok &= ExpectNear(snapshot.frametime_variance_ms2,
                     0.0,
                     0.0001,
                     "stable frametimes should have near-zero variance");
    return ok;
}

bool TestRollingHistoryLimit() {
    framewatch::MetricsEngine metrics(5);

    for (std::uint64_t i = 1; i <= 10; ++i) {
        metrics.PushSample(MakeSample(i, static_cast<double>(i), 10.0 + static_cast<double>(i)));
    }

    const framewatch::MetricsSnapshot snapshot = metrics.Snapshot();
    const std::vector<double> history = metrics.RecentFrametimeHistory();

    bool ok = true;
    ok &= Expect(snapshot.sample_count == 5, "history limit should cap rolling sample count");
    ok &= Expect(history.size() == 5, "recent history should honor the rolling window");
    ok &= ExpectNear(history.front(), 16.0, 0.001, "oldest retained sample should match window");
    ok &= ExpectNear(history.back(), 20.0, 0.001, "latest retained sample should be last pushed");
    ok &= ExpectNear(snapshot.latest_frametime_ms,
                     20.0,
                     0.001,
                     "latest frametime should follow newest sample");
    return ok;
}

bool TestPerformanceSessionBenchmarkLifecycle() {
    framewatch::PerformanceSession session(256, 256);
    session.ResetSyntheticTimeline();

    for (int i = 0; i < 30; ++i) {
        session.CaptureSyntheticFrame(16.666667);
    }

    session.StartBenchmark();
    for (int i = 0; i < 90; ++i) {
        session.CaptureSyntheticFrame(16.666667);
    }

    framewatch::BenchmarkSummary active = session.CurrentBenchmark();

    bool ok = true;
    ok &= Expect(active.active, "benchmark should report active after StartBenchmark");
    ok &= Expect(active.has_data, "benchmark should collect samples while recording");
    ok &= Expect(active.frame_count == 90, "benchmark should count only benchmark frames");
    ok &= ExpectNear(active.duration_seconds,
                     1.5,
                     0.02,
                     "benchmark duration should sum frametimes");

    session.StopBenchmark();
    const framewatch::BenchmarkSummary stopped = session.CurrentBenchmark();

    ok &= Expect(!stopped.active, "benchmark should stop after StopBenchmark");
    ok &= Expect(stopped.frame_count == 90, "stopped benchmark should retain the last run");
    ok &= ExpectNear(stopped.metrics.average_fps,
                     60.0,
                     0.05,
                     "benchmark average fps should remain stable");
    return ok;
}

bool TestOverlayRuntimePresentFlow() {
    auto renderer = std::make_unique<RecordingOverlayRenderer>();
    RecordingOverlayRenderer* renderer_ptr = renderer.get();

    framewatch::OverlayRuntime runtime(std::move(renderer), 128, 128);

    bool ok = true;
    ok &= Expect(runtime.Initialize(), "overlay runtime should initialize the renderer");

    constexpr std::uintptr_t kSwapChainTag = 0x1234;
    auto timestamp = framewatch::FrameClock::time_point{};
    framewatch::PresentEvent first_present;
    first_present.api = framewatch::GraphicsApi::Dx11;
    first_present.timestamp = timestamp;
    first_present.native_swap_chain = reinterpret_cast<void*>(kSwapChainTag);
    first_present.sync_interval = 1;

    ok &= Expect(!runtime.OnPresent(first_present),
                 "first present should only prime frametime tracking");

    framewatch::PresentEvent second_present = first_present;
    second_present.timestamp += std::chrono::milliseconds(16);

    ok &= Expect(runtime.OnPresent(second_present),
                 "second present should produce an overlay snapshot");
    ok &= Expect(renderer_ptr->render_calls == 1,
                 "overlay renderer should receive the rendered snapshot");
    ok &= Expect(runtime.LastSnapshot() != nullptr,
                 "overlay runtime should keep the last snapshot");
    ok &= Expect(runtime.Session().LiveSampleCount() == 1,
                 "overlay runtime should feed the shared performance session");
    ok &= Expect(renderer_ptr->last_present.native_swap_chain ==
                     reinterpret_cast<void*>(kSwapChainTag),
                 "overlay runtime should forward native present context to the renderer");
    ok &= Expect(renderer_ptr->last_snapshot.graph_label == std::string("LIVE GRAPH"),
                 "overlay runtime should annotate the overlay snapshot with the graph label");

    runtime.StartBenchmark();
    framewatch::PresentEvent third_present = second_present;
    third_present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(third_present);
    const framewatch::BenchmarkSummary active = runtime.Session().CurrentBenchmark();
    ok &= Expect(active.active, "runtime benchmark control should start a recording");
    ok &= Expect(active.frame_count == 1,
                 "benchmark should count frames captured after StartBenchmark");

    runtime.StopBenchmark();
    const framewatch::BenchmarkSummary stopped = runtime.Session().CurrentBenchmark();
    ok &= Expect(!stopped.active, "runtime benchmark control should stop the recording");
    ok &= Expect(stopped.frame_count == 1,
                 "stopped benchmark should retain the last captured frame count");

    runtime.Shutdown();
    ok &= Expect(!runtime.IsInitialized(), "runtime should report shutdown state");
    ok &= Expect(!renderer_ptr->initialized, "renderer shutdown should be called");
    return ok;
}

bool TestOverlayRuntimeRendererActions() {
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

    bool ok = true;
    ok &= Expect(runtime.Initialize(), "overlay runtime should initialize for action tests");

    auto timestamp = framewatch::FrameClock::time_point{};
    framewatch::PresentEvent present;
    present.api = framewatch::GraphicsApi::Dx11;
    present.native_swap_chain = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xD00D));
    present.timestamp = timestamp;
    runtime.OnPresent(present);

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    ok &= Expect(runtime.Session().IsBenchmarkRecording(),
                 "renderer actions should be able to toggle benchmark recording on");

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    ok &= Expect(!runtime.Session().IsBenchmarkRecording(),
                 "renderer actions should be able to stop benchmark recording");
    ok &= Expect(renderer_ptr->last_snapshot.status_text == std::string("BENCHMARK START"),
                 "the next rendered snapshot should expose benchmark-start status feedback");

    const auto benchmark = runtime.Session().CurrentBenchmark();
    ok &= Expect(benchmark.frame_count == 1,
                 "benchmark actions should retain the frame captured while the benchmark was active");
    ok &= Expect(std::filesystem::exists(csv_path),
                 "renderer export action should emit the csv export");
    ok &= Expect(std::filesystem::exists(json_path),
                 "renderer export action should emit the json export");

    present.timestamp += std::chrono::milliseconds(16);
    runtime.OnPresent(present);
    ok &= Expect(renderer_ptr->last_snapshot.status_text == std::string("EXPORT OK"),
                 "the next rendered snapshot should expose export status feedback");
    ok &= Expect(runtime.Session().LiveSampleCount() == 0,
                 "renderer reset action should clear the live session");
    ok &= Expect(runtime.LastSnapshot() == nullptr,
                 "renderer reset action should clear the cached overlay snapshot");
    ok &= Expect(!runtime.Session().IsBenchmarkRecording(),
                 "renderer reset action should leave benchmark recording disabled");

    present.timestamp += std::chrono::milliseconds(16);
    ok &= Expect(!runtime.OnPresent(present),
                 "the first present after a reset should re-prime frametime tracking");
    present.timestamp += std::chrono::milliseconds(16);
    ok &= Expect(runtime.OnPresent(present),
                 "the second present after a reset should render again");
    ok &= Expect(renderer_ptr->last_snapshot.status_text == std::string("SESSION RESET"),
                 "the next rendered snapshot should expose session-reset status feedback");

    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    runtime.Shutdown();
    return ok;
}

bool TestOverlaySettingsControls() {
    framewatch::OverlaySettings settings;

    bool ok = true;
    ok &= ExpectNear(framewatch::ClampOverlayOpacity(0.15),
                     0.35,
                     0.0001,
                     "overlay opacity should clamp to the minimum");
    ok &= ExpectNear(framewatch::ClampOverlayOpacity(1.5),
                     1.0,
                     0.0001,
                     "overlay opacity should clamp to the maximum");

    framewatch::AdjustOverlayOpacity(settings, -0.75);
    ok &= ExpectNear(settings.panel_opacity,
                     0.35,
                     0.0001,
                     "opacity adjustment should respect the lower bound");

    ok &= Expect(framewatch::ClampTargetFps(5) == 10,
                 "target fps should clamp to the lower bound");
    ok &= Expect(framewatch::ClampTargetFps(5'000) == 1'000,
                 "target fps should clamp to the upper bound");
    ok &= Expect(framewatch::CycleTargetFps(60, 1) == 90,
                 "cycling target fps up should advance to the next preset");
    ok &= Expect(framewatch::CycleTargetFps(60, -1) == 30,
                 "cycling target fps down should step to the previous preset");
    ok &= Expect(framewatch::CycleTargetFps(58, 1) == 90,
                 "cycling should snap a near value to its closest preset before stepping");
    ok &= Expect(framewatch::CycleTargetFps(240, 1) == 30,
                 "cycling target fps should wrap around the preset list");

    settings.dock_anchor = framewatch::OverlayDockAnchor::LeftBottom;
    settings.dock_anchor = framewatch::CycleOverlayDockAnchor(settings.dock_anchor);
    ok &= Expect(settings.dock_anchor == framewatch::OverlayDockAnchor::RightTop,
                 "dock anchor cycling should wrap to the first anchor");
    ok &= Expect(framewatch::OverlayDockAnchorName(framewatch::OverlayDockAnchor::RightBottom) ==
                     std::string_view("RIGHT BOTTOM"),
                 "dock anchor naming should stay stable");
    return ok;
}

bool TestMetricsVarianceCorrectness() {
    // {10, 20, 30, 40}: mean=25, population variance=125
    framewatch::MetricsEngine metrics(10);
    for (double ft : {10.0, 20.0, 30.0, 40.0}) {
        framewatch::FrameSample s;
        s.frametime_ms = ft;
        s.fps = 1000.0 / ft;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();
    bool ok = true;
    ok &= ExpectNear(snap.frametime_variance_ms2, 125.0, 0.001,
                     "variance of {10,20,30,40} should be 125 ms2");
    ok &= ExpectNear(snap.average_frametime_ms, 25.0, 0.001,
                     "average frametime should be 25 ms");
    ok &= ExpectNear(snap.average_fps, 1000.0 / 25.0, 0.001,
                     "average_fps should equal 1000/average_frametime_ms");
    return ok;
}

bool TestMetricsVarianceRollingEviction() {
    // Window=3, push {10,20,30,40}. Window holds {20,30,40}.
    // mean=30, population variance=((20-30)²+(30-30)²+(40-30)²)/3 = 200/3
    framewatch::MetricsEngine metrics(3);
    for (double ft : {10.0, 20.0, 30.0, 40.0}) {
        framewatch::FrameSample s;
        s.frametime_ms = ft;
        s.fps = 1000.0 / ft;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();
    bool ok = true;
    ok &= ExpectNear(snap.average_frametime_ms, 30.0, 0.001,
                     "rolling mean after eviction should be 30");
    ok &= ExpectNear(snap.frametime_variance_ms2, 200.0 / 3.0, 0.001,
                     "rolling variance after eviction should be 200/3");
    ok &= ExpectNear(snap.average_fps, 1000.0 / snap.average_frametime_ms, 0.001,
                     "average_fps must equal 1000/average_frametime_ms after eviction");
    return ok;
}

bool TestMetricsNoNegativeVariance() {
    // Constant frametimes over many samples (exceeds window) — variance must stay >= 0
    framewatch::MetricsEngine metrics(256);
    for (int i = 0; i < 500; ++i) {
        framewatch::FrameSample s;
        s.frametime_ms = 16.666667;
        s.fps = 60.0;
        metrics.PushSample(s);
    }
    const auto snap = metrics.Snapshot();
    bool ok = true;
    ok &= Expect(snap.frametime_variance_ms2 >= 0.0,
                 "variance must never be negative");
    ok &= ExpectNear(snap.frametime_variance_ms2, 0.0, 1e-6,
                     "variance of constant frametimes must be near zero");
    return ok;
}

bool TestExporterRoundTrip() {
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

    bool ok = true;
    ok &= Expect(framewatch::ExportSamplesToCsv(samples, csv_path),
                 "csv exporter should succeed");
    ok &= Expect(std::filesystem::exists(csv_path), "csv file should be created");
    ok &= Expect(framewatch::ExportSamplesToJson(samples, json_path),
                 "json exporter should succeed");
    ok &= Expect(std::filesystem::exists(json_path), "json file should be created");

    std::ifstream csv_file(csv_path);
    ok &= Expect(csv_file.is_open(), "csv should be readable");
    std::string line;
    int line_count = 0;
    while (std::getline(csv_file, line)) {
        ++line_count;
    }
    ok &= Expect(line_count == 6, "csv should have 1 header + 5 data rows");

    std::filesystem::remove(csv_path);
    std::filesystem::remove(json_path);
    return ok;
}

bool TestOverlaySettingsPersistence() {
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

    bool ok = true;
    ok &= Expect(framewatch::SaveOverlaySettings(settings, path),
                 "overlay settings should save to json");

    const auto loaded = framewatch::LoadOverlaySettings(path);
    ok &= Expect(loaded.has_value(), "overlay settings should load from json");
    if (loaded.has_value()) {
        ok &= Expect(!loaded->show_overlay, "loaded settings should preserve show_overlay");
        ok &= Expect(!loaded->show_graph, "loaded settings should preserve show_graph");
        ok &= Expect(!loaded->show_sidebar, "loaded settings should preserve show_sidebar");
        ok &= Expect(!loaded->show_hotkey_hints,
                     "loaded settings should preserve show_hotkey_hints");
        ok &= Expect(loaded->show_settings_panel,
                     "loaded settings should preserve show_settings_panel");
        ok &= Expect(loaded->capture_input_when_panel_open,
                     "loaded settings should preserve capture_input_when_panel_open");
        ok &= Expect(loaded->compact_mode,
                     "loaded settings should preserve compact_mode");
        ok &= ExpectNear(loaded->panel_opacity,
                         0.55,
                         0.0001,
                         "loaded settings should preserve panel opacity");
        ok &= Expect(loaded->target_fps == 144,
                     "loaded settings should preserve target fps");
        ok &= Expect(loaded->dock_anchor == framewatch::OverlayDockAnchor::LeftTop,
                     "loaded settings should preserve dock anchor");
        ok &= Expect(loaded->follow_target_window,
                     "loaded settings should preserve follow_target_window");
        ok &= Expect(loaded->target_window_query == std::string("Game \"Window\" \\\\ DX11"),
                     "loaded settings should preserve escaped target query");
        ok &= Expect(loaded->window_width == 1320,
                     "loaded settings should preserve window width");
        ok &= Expect(loaded->window_height == 840,
                     "loaded settings should preserve window height");
        ok &= Expect(loaded->window_x == std::optional<int>(144),
                     "loaded settings should preserve window x");
        ok &= Expect(loaded->window_y == std::optional<int>(96),
                     "loaded settings should preserve window y");
    }

    std::filesystem::remove(path);
    return ok;
}

bool TestHookOverlayServiceWiring() {
    auto hook = std::make_unique<RecordingPresentHook>();
    RecordingPresentHook* hook_ptr = hook.get();

    auto renderer = std::make_unique<RecordingOverlayRenderer>();
    RecordingOverlayRenderer* renderer_ptr = renderer.get();

    framewatch::HookOverlayService service(std::move(hook), std::move(renderer), 128, 128);

    bool ok = true;
    ok &= Expect(service.Initialize(), "hook overlay service should initialize");
    ok &= Expect(service.IsInitialized(), "service should report initialized state");
    ok &= Expect(hook_ptr->callback_was_set, "service should register a present callback");
    ok &= Expect(hook_ptr->install_was_called, "service should install the present hook");

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

    ok &= Expect(renderer_ptr->render_calls == 1,
                 "present callback should flow into overlay rendering");
    ok &= Expect(service.Runtime().Session().LiveSampleCount() == 1,
                 "hook overlay service should feed the runtime session");
    ok &= Expect(renderer_ptr->last_present.native_swap_chain ==
                     reinterpret_cast<void*>(kSwapChainTag),
                 "hook overlay service should preserve native present context");

    service.Shutdown();
    ok &= Expect(hook_ptr->remove_was_called, "service shutdown should remove the hook");
    ok &= Expect(!service.IsInitialized(), "service should report shutdown state");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= TestFrametimeTracker();
    ok &= TestStableMetrics();
    ok &= TestRollingHistoryLimit();
    ok &= TestMetricsVarianceCorrectness();
    ok &= TestMetricsVarianceRollingEviction();
    ok &= TestMetricsNoNegativeVariance();
    ok &= TestExporterRoundTrip();
    ok &= TestPerformanceSessionBenchmarkLifecycle();
    ok &= TestOverlayRuntimePresentFlow();
    ok &= TestOverlayRuntimeRendererActions();
    ok &= TestOverlaySettingsControls();
    ok &= TestOverlaySettingsPersistence();
    ok &= TestHookOverlayServiceWiring();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "All FrameWatch tests passed\n";
    return EXIT_SUCCESS;
}
