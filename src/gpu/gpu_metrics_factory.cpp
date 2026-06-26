#include "framewatch/gpu/gpu_metrics.h"

namespace framewatch {

// Forward declarations from each provider file.
std::unique_ptr<IGpuMetricsProvider> MakeNvmlGpuProvider();
std::unique_ptr<IGpuMetricsProvider> MakeSysfsGpuProvider();
std::unique_ptr<IGpuMetricsProvider> MakeNullGpuProvider();

std::unique_ptr<IGpuMetricsProvider> CreateGpuMetricsProvider() {
    // NVML works on Linux and Windows when NVIDIA drivers are installed.
    if (auto p = MakeNvmlGpuProvider()) return p;

    // Linux sysfs covers AMD (amdgpu) and Intel (i915/xe).
    if (auto p = MakeSysfsGpuProvider()) return p;

    return MakeNullGpuProvider();
}

}  // namespace framewatch
