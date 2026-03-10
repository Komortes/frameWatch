#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "framewatch/core/frametime_tracker.h"
#include "framewatch/core/metrics_engine.h"

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

}  // namespace

int main() {
    bool ok = true;
    ok &= TestFrametimeTracker();
    ok &= TestStableMetrics();
    ok &= TestRollingHistoryLimit();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "All FrameWatch tests passed\n";
    return EXIT_SUCCESS;
}
