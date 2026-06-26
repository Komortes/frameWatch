#include "framewatch/gpu/gpu_metrics_sampler.h"

namespace framewatch {

GpuMetricsSampler::GpuMetricsSampler(std::chrono::milliseconds interval)
    : provider_(CreateGpuMetricsProvider()), interval_(interval) {}

GpuMetricsSampler::~GpuMetricsSampler() { Stop(); }

void GpuMetricsSampler::Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&GpuMetricsSampler::ThreadFn, this);
}

void GpuMetricsSampler::Stop() noexcept {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

GpuMetrics GpuMetricsSampler::LastSample() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_sample_;
}

const char* GpuMetricsSampler::ProviderName() const noexcept {
    return provider_ ? provider_->ProviderName() : "null";
}

void GpuMetricsSampler::ThreadFn() {
    while (running_.load()) {
        GpuMetrics sample = provider_->Query();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_sample_ = std::move(sample);
        }
        // Sleep in short slices so Stop() is responsive.
        const int slices = std::max(1, static_cast<int>(interval_.count() / 50));
        for (int i = 0; i < slices && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }
}

}  // namespace framewatch
