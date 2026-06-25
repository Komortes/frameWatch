#include "framewatch/core/metrics_engine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace framewatch {

namespace {

// Read percentile from an already-sorted multiset using shortest-path iterator
// advance: O(min(idx, n-1-idx)), which for 1%/0.1% lows (high frametime percentiles)
// is O(n*(1-p)) ≈ O(20) or O(2) at n=2000 — far cheaper than O(n log n) sort.
double PercentileFromSorted(const std::multiset<double>& sorted, double p) {
    const std::size_t n = sorted.size();
    if (n == 0) return 0.0;
    if (n == 1) return *sorted.begin();

    const double clamped = std::clamp(p, 0.0, 1.0);
    const double scaled = clamped * static_cast<double>(n - 1);
    const auto lower_idx = static_cast<std::size_t>(std::floor(scaled));
    const auto upper_idx = static_cast<std::size_t>(std::ceil(scaled));
    const double fraction = scaled - static_cast<double>(lower_idx);

    auto get_at = [&](std::size_t idx) -> double {
        const std::size_t from_end = n - 1 - idx;
        if (from_end < idx) {
            return *std::prev(sorted.end(), static_cast<std::ptrdiff_t>(from_end + 1));
        }
        return *std::next(sorted.begin(), static_cast<std::ptrdiff_t>(idx));
    };

    if (lower_idx == upper_idx) {
        return get_at(lower_idx);
    }
    const double lo = get_at(lower_idx);
    const double hi = get_at(upper_idx);
    return lo + (hi - lo) * fraction;
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

        // Evict the oldest value from the sorted mirror.
        const auto it = sorted_frametimes_ms_.find(y);
        if (it != sorted_frametimes_ms_.end()) {
            sorted_frametimes_ms_.erase(it);
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
    sorted_frametimes_ms_.insert(x);
    total_time_ms_ += x;
    latest_frametime_ms_ = x;
    ++total_sample_count_;
}

void MetricsEngine::Reset() noexcept {
    frametimes_ms_.clear();
    sorted_frametimes_ms_.clear();
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

    // O(1) min/max from the sorted mirror.
    snapshot.min_frametime_ms = *sorted_frametimes_ms_.begin();
    snapshot.max_frametime_ms = *sorted_frametimes_ms_.rbegin();

    // O(n*(1-p)) percentile instead of O(n log n) copy+sort.
    const double one_percent_slowest_ms =
        PercentileFromSorted(sorted_frametimes_ms_, 0.99);
    const double point_one_percent_slowest_ms =
        PercentileFromSorted(sorted_frametimes_ms_, 0.999);

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
