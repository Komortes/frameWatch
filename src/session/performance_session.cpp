#include "framewatch/session/performance_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace framewatch {

namespace {

double SessionDurationSeconds(const SessionLogger& logger) {
    const auto& samples = logger.Samples();
    if (samples.empty()) {
        return 0.0;
    }
    const double total_frametime_ms = std::accumulate(
        samples.begin(),
        samples.end(),
        0.0,
        [](double total, const FrameSample& sample) { return total + sample.frametime_ms; });
    return total_frametime_ms / 1'000.0;
}

}  // namespace

PerformanceSession::PerformanceSession(std::size_t live_history_limit,
                                       std::size_t benchmark_history_limit)
    : live_metrics_(live_history_limit),
      benchmark_metrics_(benchmark_history_limit) {}

void PerformanceSession::Reset() {
    live_tracker_.Reset();
    live_metrics_.Reset();
    live_logger_.Clear();
    benchmark_metrics_.Reset();
    benchmark_logger_.Clear();
    last_completed_benchmark_.reset();
    benchmark_recording_ = false;
    synthetic_timestamp_ = FrameClock::time_point{};
    synthetic_timeline_ready_ = false;
}

void PerformanceSession::ResetSyntheticTimeline(FrameClock::time_point initial_timestamp) {
    Reset();
    synthetic_timestamp_ = initial_timestamp;
    synthetic_timeline_ready_ = true;
    live_tracker_.Capture(initial_timestamp);
}

std::optional<FrameSample> PerformanceSession::CaptureFrame(FrameClock::time_point timestamp) {
    const std::optional<FrameSample> sample = live_tracker_.Capture(timestamp);
    if (!sample.has_value()) {
        return std::nullopt;
    }

    live_metrics_.PushSample(*sample);
    live_logger_.Append(*sample);

    if (benchmark_recording_) {
        benchmark_metrics_.PushSample(*sample);
        benchmark_logger_.Append(*sample);
    }

    return sample;
}

std::optional<FrameSample> PerformanceSession::CaptureSyntheticFrame(double frametime_ms) {
    if (!synthetic_timeline_ready_) {
        ResetSyntheticTimeline();
    }

    synthetic_timestamp_ += std::chrono::microseconds(
        static_cast<long long>(std::llround(frametime_ms * 1'000.0)));
    return CaptureFrame(synthetic_timestamp_);
}

void PerformanceSession::StartBenchmark() {
    benchmark_metrics_.Reset();
    benchmark_logger_.Clear();
    benchmark_recording_ = true;
    last_completed_benchmark_.reset();
}

void PerformanceSession::StopBenchmark() {
    if (!benchmark_recording_) {
        return;
    }

    benchmark_recording_ = false;
    if (benchmark_logger_.Size() > 0) {
        last_completed_benchmark_ = BuildBenchmarkSummary();
        last_completed_benchmark_->active = false;
    }
}

void PerformanceSession::ToggleBenchmark() {
    if (benchmark_recording_) {
        StopBenchmark();
    } else {
        StartBenchmark();
    }
}

bool PerformanceSession::IsBenchmarkRecording() const noexcept {
    return benchmark_recording_;
}

std::size_t PerformanceSession::LiveSampleCount() const noexcept {
    return live_logger_.Size();
}

MetricsSnapshot PerformanceSession::LiveMetrics() const {
    return live_metrics_.Snapshot();
}

BenchmarkSummary PerformanceSession::BuildBenchmarkSummary() const {
    BenchmarkSummary summary;
    summary.active = benchmark_recording_;
    summary.has_data = benchmark_logger_.Size() > 0;
    summary.frame_count = benchmark_logger_.Size();
    summary.duration_seconds = SessionDurationSeconds(benchmark_logger_);
    summary.metrics = benchmark_metrics_.Snapshot();
    return summary;
}

BenchmarkSummary PerformanceSession::CurrentBenchmark() const {
    if (benchmark_recording_ || benchmark_logger_.Size() > 0) {
        return BuildBenchmarkSummary();
    }
    if (last_completed_benchmark_.has_value()) {
        return *last_completed_benchmark_;
    }
    return {};
}

OverlaySnapshot PerformanceSession::GraphSnapshot() const {
    if (benchmark_logger_.Size() > 1) {
        return overlay_model_.Build(benchmark_metrics_.Snapshot(),
                                    benchmark_metrics_.RecentFrametimeHistory());
    }
    return overlay_model_.Build(live_metrics_.Snapshot(), live_metrics_.RecentFrametimeHistory());
}

std::string PerformanceSession::GraphLabel() const {
    if (benchmark_recording_) {
        return "BENCHMARK GRAPH";
    }
    if (benchmark_logger_.Size() > 1) {
        return "LAST BENCHMARK";
    }
    return "LIVE GRAPH";
}

bool PerformanceSession::ExportPreferred(const std::filesystem::path& csv_path,
                                         const std::filesystem::path& json_path) const {
    const SessionLogger& source = benchmark_logger_.Size() > 0 ? benchmark_logger_ : live_logger_;
    const bool csv_ok = source.ExportCsv(csv_path);
    const bool json_ok = source.ExportJson(json_path);
    return csv_ok && json_ok;
}

const SessionLogger& PerformanceSession::LiveLogger() const noexcept {
    return live_logger_;
}

const SessionLogger& PerformanceSession::BenchmarkLogger() const noexcept {
    return benchmark_logger_;
}

}  // namespace framewatch
