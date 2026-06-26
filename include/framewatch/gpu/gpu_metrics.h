#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace framewatch {

struct GpuMetrics {
    bool        available{false};
    std::string gpu_name;
    float       gpu_load_percent{0.f};
    float       gpu_temp_c{0.f};
    uint64_t    vram_used_bytes{0};
    uint64_t    vram_total_bytes{0};
};

class IGpuMetricsProvider {
  public:
    virtual ~IGpuMetricsProvider() = default;
    virtual GpuMetrics  Query()                     = 0;
    virtual const char* ProviderName() const noexcept = 0;
};

/** Factory: tries NVML → sysfs (Linux) → Null. Never returns nullptr. */
std::unique_ptr<IGpuMetricsProvider> CreateGpuMetricsProvider();

}  // namespace framewatch
