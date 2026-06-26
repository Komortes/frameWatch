#pragma once

#include "gpu_metrics.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace framewatch {

/**
 * Polls a GpuMetricsProvider on a background thread at a fixed interval.
 * LastSample() is always safe to call from any thread.
 */
class GpuMetricsSampler {
  public:
    explicit GpuMetricsSampler(
        std::chrono::milliseconds interval = std::chrono::milliseconds{500});
    ~GpuMetricsSampler();

    GpuMetricsSampler(const GpuMetricsSampler&)            = delete;
    GpuMetricsSampler& operator=(const GpuMetricsSampler&) = delete;

    void Start();
    void Stop() noexcept;

    bool       IsRunning()    const noexcept { return running_.load(); }
    GpuMetrics LastSample()   const noexcept;
    const char* ProviderName() const noexcept;

  private:
    void ThreadFn();

    std::unique_ptr<IGpuMetricsProvider> provider_;
    std::chrono::milliseconds            interval_;
    std::atomic<bool>                    running_{false};
    std::thread                          thread_;
    mutable std::mutex                   mutex_;
    GpuMetrics                           last_sample_;
};

}  // namespace framewatch
