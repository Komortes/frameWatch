#include "framewatch/gpu/gpu_metrics.h"

namespace framewatch {
namespace {

class NullGpuProvider final : public IGpuMetricsProvider {
  public:
    GpuMetrics  Query() override               { return {}; }
    const char* ProviderName() const noexcept override { return "null"; }
};

}  // namespace

// Defined in gpu_metrics_factory.cpp — forward-declared here for visibility.
// NullGpuProvider is used as the fallback returned from CreateGpuMetricsProvider().
std::unique_ptr<IGpuMetricsProvider> MakeNullGpuProvider() {
    return std::make_unique<NullGpuProvider>();
}

}  // namespace framewatch
