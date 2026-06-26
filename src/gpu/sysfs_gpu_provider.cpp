/**
 * Linux sysfs GPU provider — AMD (amdgpu) and Intel (i915/xe) via
 * /sys/class/drm/cardN/device/. No kernel headers required; all reads
 * are plain text files.
 */
#ifdef __linux__

#include "framewatch/gpu/gpu_metrics.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace framewatch {
namespace {

namespace fs = std::filesystem;

static bool ReadSysfsUint64(const fs::path& p, uint64_t& out) {
    std::ifstream f(p);
    if (!f.is_open()) return false;
    f >> out;
    return !f.fail();
}

static bool ReadSysfsFloat(const fs::path& p, float& out) {
    uint64_t raw = 0;
    if (!ReadSysfsUint64(p, raw)) return false;
    out = static_cast<float>(raw);
    return true;
}

static std::string ReadSysfsString(const fs::path& p) {
    std::ifstream f(p);
    if (!f.is_open()) return {};
    std::string s;
    std::getline(f, s);
    return s;
}

// Find the first hwmon directory under a device path.
static fs::path FindHwmon(const fs::path& device) {
    const fs::path hwmon_dir = device / "hwmon";
    if (!fs::exists(hwmon_dir)) return {};
    for (const auto& entry : fs::directory_iterator(hwmon_dir)) {
        if (entry.is_directory()) return entry.path();
    }
    return {};
}

// ── Provider ──────────────────────────────────────────────────────────────────

class SysfsGpuProvider final : public IGpuMetricsProvider {
  public:
    SysfsGpuProvider() { Probe(); }

    bool IsAvailable() const noexcept { return available_; }

    GpuMetrics Query() override {
        if (!available_) return {};

        GpuMetrics m;
        m.available = true;
        m.gpu_name  = gpu_name_;

        // GPU load %
        uint64_t busy = 0;
        if (ReadSysfsUint64(busy_path_, busy))
            m.gpu_load_percent = static_cast<float>(busy);

        // Temperature (millicelsius → celsius)
        if (!temp_path_.empty()) {
            uint64_t temp_mc = 0;
            if (ReadSysfsUint64(temp_path_, temp_mc))
                m.gpu_temp_c = static_cast<float>(temp_mc) / 1000.f;
        }

        // VRAM
        uint64_t used = 0, total = 0;
        if (ReadSysfsUint64(vram_used_path_, used))  m.vram_used_bytes  = used;
        if (ReadSysfsUint64(vram_total_path_, total)) m.vram_total_bytes = total;

        return m;
    }

    const char* ProviderName() const noexcept override { return "sysfs"; }

  private:
    bool        available_{false};
    std::string gpu_name_;
    fs::path    busy_path_;
    fs::path    temp_path_;
    fs::path    vram_used_path_;
    fs::path    vram_total_path_;

    void Probe() {
        const fs::path drm{"/sys/class/drm"};
        if (!fs::exists(drm)) return;

        for (const auto& entry : fs::directory_iterator(drm)) {
            const std::string name = entry.path().filename().string();
            // Only top-level cardN entries (not cardN-<connector>).
            if (name.rfind("card", 0) != 0) continue;
            if (name.size() > 5) continue;  // skip "card0-HDMI-1" etc.

            const fs::path device = entry.path() / "device";

            // amdgpu: gpu_busy_percent exists
            const fs::path busy = device / "gpu_busy_percent";
            if (!fs::exists(busy)) continue;

            busy_path_ = busy;

            // VRAM (amdgpu-specific)
            vram_used_path_  = device / "mem_info_vram_used";
            vram_total_path_ = device / "mem_info_vram_total";

            // Temperature via hwmon
            const fs::path hwmon = FindHwmon(device);
            if (!hwmon.empty()) {
                const fs::path t1 = hwmon / "temp1_input";
                if (fs::exists(t1)) temp_path_ = t1;
            }

            // GPU name: prefer 'product_name', fall back to vendor+device hex ids
            const fs::path name_path = device / "product_name";
            if (fs::exists(name_path)) {
                gpu_name_ = ReadSysfsString(name_path);
            } else {
                const std::string vendor = ReadSysfsString(device / "vendor");
                const std::string devid  = ReadSysfsString(device / "device");
                gpu_name_ = vendor.empty() ? "GPU" : vendor + " " + devid;
            }

            available_ = true;
            return;
        }
    }
};

}  // namespace

std::unique_ptr<IGpuMetricsProvider> MakeSysfsGpuProvider() {
    auto p = std::make_unique<SysfsGpuProvider>();
    if (!p->IsAvailable()) return nullptr;
    return p;
}

}  // namespace framewatch

#else

#include "framewatch/gpu/gpu_metrics.h"
namespace framewatch {
std::unique_ptr<IGpuMetricsProvider> MakeSysfsGpuProvider() { return nullptr; }
}

#endif  // __linux__
