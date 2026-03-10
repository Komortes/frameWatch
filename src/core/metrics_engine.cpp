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

    return sorted[lower_index] +
           ((sorted[upper_index] - sorted[lower_index]) * fraction);
}

}  // namespace

MetricsEngine::MetricsEngine(std::size_t history_limit)
    : history_limit_(std::max<std::size_t>(1, history_limit)) {}

void MetricsEngine::PushSample(const FrameSample& sample) {
    frametimes_ms_.push_back(sample.frametime_ms);
    rolling_sum_frametimes_ms_ += sample.frametime_ms;
    rolling_sum_squares_ms_ += sample.frametime_ms * sample.frametime_ms;
    total_time_ms_ += sample.frametime_ms;
    latest_frametime_ms_ = sample.frametime_ms;
    ++total_sample_count_;

    if (frametimes_ms_.size() > history_limit_) {
        const double removed = frametimes_ms_.front();
        frametimes_ms_.pop_front();
        rolling_sum_frametimes_ms_ -= removed;
        rolling_sum_squares_ms_ -= removed * removed;
    }
}

void MetricsEngine::Reset() noexcept {
    frametimes_ms_.clear();
    rolling_sum_frametimes_ms_ = 0.0;
    rolling_sum_squares_ms_ = 0.0;
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

    snapshot.latest_frametime_ms = latest_frametime_ms_;
    snapshot.current_fps =
        latest_frametime_ms_ > 0.0 ? (1'000.0 / latest_frametime_ms_) : 0.0;
    snapshot.average_fps =
        total_time_ms_ > 0.0 ? ((static_cast<double>(total_sample_count_) * 1'000.0) / total_time_ms_)
                             : 0.0;

    snapshot.average_frametime_ms =
        rolling_sum_frametimes_ms_ / static_cast<double>(frametimes_ms_.size());

    const double variance =
        (rolling_sum_squares_ms_ / static_cast<double>(frametimes_ms_.size())) -
        (snapshot.average_frametime_ms * snapshot.average_frametime_ms);
    snapshot.frametime_variance_ms2 = std::max(0.0, variance);

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
