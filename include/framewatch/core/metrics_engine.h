#pragma once

#include <cstddef>
#include <deque>
#include <set>
#include <vector>

#include "framewatch/core/frame_sample.h"

namespace framewatch {

struct MetricsSnapshot {
    std::size_t sample_count{0};
    double current_fps{0.0};
    double average_fps{0.0};
    double one_percent_low_fps{0.0};
    double point_one_percent_low_fps{0.0};
    double latest_frametime_ms{0.0};
    double average_frametime_ms{0.0};
    double frametime_variance_ms2{0.0};
    double min_frametime_ms{0.0};
    double max_frametime_ms{0.0};
};

class MetricsEngine {
  public:
    explicit MetricsEngine(std::size_t history_limit = 2'000);

    void PushSample(const FrameSample& sample);
    void Reset() noexcept;

    MetricsSnapshot Snapshot() const;
    std::vector<double> RecentFrametimeHistory() const;

  private:
    std::size_t history_limit_;
    std::deque<double> frametimes_ms_;
    std::multiset<double> sorted_frametimes_ms_;
    double welford_mean_{0.0};
    double welford_M2_{0.0};
    double total_time_ms_{0.0};
    double latest_frametime_ms_{0.0};
    std::size_t total_sample_count_{0};
};

}  // namespace framewatch
