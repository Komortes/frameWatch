#include "framewatch/core/metrics_engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace framewatch {

namespace {

double FrametimePercentile(const std::deque<double>& frametimes, double percentile) {
    if (frametimes.empty()) {
        return 0.0;
    }

    std::vector<double> sorted(frametimes.begin(), frametimes.end());
    std::sort(sorted.begin(), sorted.end());

    if (sorted.size() == 1) {
        return sorted.front();
    }

    const double clamped_percentile = std::clamp(percentile, 0.0, 1.0);
    const double scaled_index = clamped_percentile * static_cast<double>(sorted.size() - 1);
    const auto lower_index = static_cast<std::size_t>(std::floor(scaled_index));
    const auto upper_index = static_cast<std::size_t>(std::ceil(scaled_index));
    const double fraction = scaled_index - static_cast<double>(lower_index);

    return sorted[lower_index] + ((sorted[upper_index] - sorted[lower_index]) * fraction);
}

}  // namespace

MetricsEngine::MetricsEngine(std::size_t history_limit)
    : history_limit_(std::max<std::size_t>(1, history_limit)) {}

void MetricsEngine::PushSample(const FrameSample& sample) {
    const double x = sample.frametime_ms;

    if (frametimes_ms_.size() >= history_limit_) {
        const double y = frametimes_ms_.front();
        const std::size_t n = frametimes_ms_.size();

        if (n == 1) {
            // Replacing the only element: skip the remove/add dance
            welford_mean_ = x;
            welford_M2_ = 0.0;
        } else {
            // Remove y: n elements → n-1 elements (Welford downdate)
            const double old_mean = welford_mean_;
            const double dn = static_cast<double>(n);
            welford_mean_ = (dn * welford_mean_ - y) / (dn - 1.0);
            welford_M2_ -= (y - old_mean) * (y - welford_mean_);
            welford_M2_ = std::max(0.0, welford_M2_);

            // Add x: n-1 elements → n elements (Welford update)
            const double delta = x - welford_mean_;
            welford_mean_ += delta / dn;
            welford_M2_ += delta * (x - welford_mean_);
        }

        frametimes_ms_.pop_front();
    } else {
        // Window not yet full: k elements → k+1 elements
        const double k1 = static_cast<double>(frametimes_ms_.size() + 1);
        const double delta = x - welford_mean_;
        welford_mean_ += delta / k1;
        welford_M2_ += delta * (x - welford_mean_);
    }

    frametimes_ms_.push_back(x);
    total_time_ms_ += x;
    latest_frametime_ms_ = x;
    ++total_sample_count_;
}

void MetricsEngine::Reset() noexcept {
    frametimes_ms_.clear();
    welford_mean_ = 0.0;
    welford_M2_ = 0.0;
    total_time_ms_ = 0.0;
    latest_frametime_ms_ = 0.0;
    total_sample_count_ = 0;
}

MetricsSnapshot MetricsEngine::Snapshot() const {
    MetricsSnapshot snapshot;
    snapshot.sample_count = frametimes_ms_.size();

    if (frametimes_ms_.empty()) {
        return snapshot;
    }

    const std::size_t n = frametimes_ms_.size();

    snapshot.latest_frametime_ms = latest_frametime_ms_;
    snapshot.current_fps = latest_frametime_ms_ > 0.0 ? (1'000.0 / latest_frametime_ms_) : 0.0;
    snapshot.average_frametime_ms = welford_mean_;
    snapshot.average_fps = welford_mean_ > 0.0 ? (1'000.0 / welford_mean_) : 0.0;
    snapshot.frametime_variance_ms2 =
        n > 1 ? std::max(0.0, welford_M2_ / static_cast<double>(n)) : 0.0;

    const auto [min_it, max_it] =
        std::minmax_element(frametimes_ms_.begin(), frametimes_ms_.end());
    snapshot.min_frametime_ms = *min_it;
    snapshot.max_frametime_ms = *max_it;

    const double one_percent_slowest_ms = FrametimePercentile(frametimes_ms_, 0.99);
    const double point_one_percent_slowest_ms = FrametimePercentile(frametimes_ms_, 0.999);

    snapshot.one_percent_low_fps =
        one_percent_slowest_ms > 0.0 ? (1'000.0 / one_percent_slowest_ms) : 0.0;
    snapshot.point_one_percent_low_fps =
        point_one_percent_slowest_ms > 0.0 ? (1'000.0 / point_one_percent_slowest_ms) : 0.0;

    return snapshot;
}

std::vector<double> MetricsEngine::RecentFrametimeHistory() const {
    return {frametimes_ms_.begin(), frametimes_ms_.end()};
}

}  // namespace framewatch
