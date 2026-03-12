#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "framewatch/core/frametime_tracker.h"
#include "framewatch/core/metrics_engine.h"
#include "framewatch/core/session_logger.h"
#include "framewatch/overlay/overlay_model.h"

namespace framewatch {

struct BenchmarkSummary {
    bool active{false};
    bool has_data{false};
    std::size_t frame_count{0};
    double duration_seconds{0.0};
    MetricsSnapshot metrics;
};

class PerformanceSession {
  public:
    explicit PerformanceSession(std::size_t live_history_limit = 360,
                                std::size_t benchmark_history_limit = 5'000);

    void Reset();
    void ResetSyntheticTimeline(FrameClock::time_point initial_timestamp = FrameClock::time_point{});

    std::optional<FrameSample> CaptureFrame(FrameClock::time_point timestamp);
    std::optional<FrameSample> CaptureSyntheticFrame(double frametime_ms);

    void StartBenchmark();
    void StopBenchmark();
    void ToggleBenchmark();

    bool IsBenchmarkRecording() const noexcept;
    std::size_t LiveSampleCount() const noexcept;

    MetricsSnapshot LiveMetrics() const;
    BenchmarkSummary CurrentBenchmark() const;
    OverlaySnapshot GraphSnapshot() const;
    std::string GraphLabel() const;

    bool ExportPreferred(const std::filesystem::path& csv_path,
                         const std::filesystem::path& json_path) const;

    const SessionLogger& LiveLogger() const noexcept;
    const SessionLogger& BenchmarkLogger() const noexcept;

  private:
    BenchmarkSummary BuildBenchmarkSummary() const;

    FrametimeTracker live_tracker_;
    MetricsEngine live_metrics_;
    SessionLogger live_logger_;

    MetricsEngine benchmark_metrics_;
    SessionLogger benchmark_logger_;
    std::optional<BenchmarkSummary> last_completed_benchmark_;
    bool benchmark_recording_{false};

    OverlayModel overlay_model_;
    FrameClock::time_point synthetic_timestamp_{};
    bool synthetic_timeline_ready_{false};
};

}  // namespace framewatch
