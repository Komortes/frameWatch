/**
 * NVML GPU provider — loads libnvidia-ml at runtime via dlopen/LoadLibrary.
 * No link-time NVML dependency; gracefully returns IsAvailable()=false when
 * NVIDIA drivers are absent.
 */
#include "framewatch/gpu/gpu_metrics.h"

#include <cstring>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace framewatch {

// ── Minimal NVML ABI (stable across driver versions) ─────────────────────────

typedef struct nvmlDevice_st* nvmlDevice_t;

typedef enum {
    NVML_SUCCESS = 0,
} nvmlReturn_t;

#define NVML_TEMPERATURE_GPU    0u
#define NVML_DEVICE_NAME_BUFFER_SIZE 96u

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

using PFN_nvmlInit          = nvmlReturn_t (*)();
using PFN_nvmlShutdown      = nvmlReturn_t (*)();
using PFN_nvmlDeviceGetHandleByIndex = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using PFN_nvmlDeviceGetName          = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using PFN_nvmlDeviceGetUtilizationRates = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using PFN_nvmlDeviceGetTemperature      = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
using PFN_nvmlDeviceGetMemoryInfo       = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);

// ── Loader ────────────────────────────────────────────────────────────────────

namespace {

class NvmlLibrary {
  public:
    NvmlLibrary() { Load(); }
    ~NvmlLibrary() { Unload(); }

    bool IsLoaded() const noexcept { return handle_ != nullptr && init_ok_; }

    PFN_nvmlDeviceGetHandleByIndex   DeviceGetHandleByIndex{};
    PFN_nvmlDeviceGetName            DeviceGetName{};
    PFN_nvmlDeviceGetUtilizationRates DeviceGetUtilizationRates{};
    PFN_nvmlDeviceGetTemperature     DeviceGetTemperature{};
    PFN_nvmlDeviceGetMemoryInfo      DeviceGetMemoryInfo{};

  private:
    void* handle_  = nullptr;
    bool  init_ok_ = false;

    void* Sym(const char* name) noexcept {
#ifdef _WIN32
        return reinterpret_cast<void*>(
            GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
        return dlsym(handle_, name);
#endif
    }

    void Load() noexcept {
#ifdef _WIN32
        handle_ = static_cast<void*>(LoadLibraryW(L"nvml.dll"));
#else
        handle_ = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!handle_) handle_ = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!handle_) return;

        // Init / shutdown are loaded but called here directly.
        auto fn_init = reinterpret_cast<PFN_nvmlInit>(Sym("nvmlInit_v2"));
        if (!fn_init) fn_init = reinterpret_cast<PFN_nvmlInit>(Sym("nvmlInit"));
        if (!fn_init || fn_init() != NVML_SUCCESS) { Unload(); return; }

        init_ok_ = true;

        DeviceGetHandleByIndex    = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex>(
            Sym("nvmlDeviceGetHandleByIndex_v2"));
        if (!DeviceGetHandleByIndex)
            DeviceGetHandleByIndex = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex>(
                Sym("nvmlDeviceGetHandleByIndex"));

        DeviceGetName              = reinterpret_cast<PFN_nvmlDeviceGetName>(
            Sym("nvmlDeviceGetName"));
        DeviceGetUtilizationRates  = reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(
            Sym("nvmlDeviceGetUtilizationRates"));
        DeviceGetTemperature       = reinterpret_cast<PFN_nvmlDeviceGetTemperature>(
            Sym("nvmlDeviceGetTemperature"));
        DeviceGetMemoryInfo        = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(
            Sym("nvmlDeviceGetMemoryInfo"));
    }

    void Unload() noexcept {
        if (handle_) {
            if (init_ok_) {
                auto fn_shutdown = reinterpret_cast<PFN_nvmlShutdown>(Sym("nvmlShutdown"));
                if (fn_shutdown) fn_shutdown();
            }
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
            handle_  = nullptr;
            init_ok_ = false;
        }
    }
};

// ── Provider ──────────────────────────────────────────────────────────────────

class NvmlGpuProvider final : public IGpuMetricsProvider {
  public:
    NvmlGpuProvider() {
        if (!lib_.IsLoaded()) return;

        nvmlDevice_t dev{};
        if (!lib_.DeviceGetHandleByIndex) return;
        if (lib_.DeviceGetHandleByIndex(0, &dev) != NVML_SUCCESS) return;

        device_    = dev;
        available_ = true;

        // Cache GPU name
        char name[NVML_DEVICE_NAME_BUFFER_SIZE]{};
        if (lib_.DeviceGetName &&
            lib_.DeviceGetName(device_, name, sizeof(name)) == NVML_SUCCESS) {
            gpu_name_ = name;
        }
    }

    bool IsAvailable() const noexcept { return available_; }

    GpuMetrics Query() override {
        if (!available_) return {};

        GpuMetrics m;
        m.available = true;
        m.gpu_name  = gpu_name_;

        if (lib_.DeviceGetUtilizationRates) {
            nvmlUtilization_t util{};
            if (lib_.DeviceGetUtilizationRates(device_, &util) == NVML_SUCCESS)
                m.gpu_load_percent = static_cast<float>(util.gpu);
        }

        if (lib_.DeviceGetTemperature) {
            unsigned int temp = 0;
            if (lib_.DeviceGetTemperature(device_, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
                m.gpu_temp_c = static_cast<float>(temp);
        }

        if (lib_.DeviceGetMemoryInfo) {
            nvmlMemory_t mem{};
            if (lib_.DeviceGetMemoryInfo(device_, &mem) == NVML_SUCCESS) {
                m.vram_used_bytes  = mem.used;
                m.vram_total_bytes = mem.total;
            }
        }

        return m;
    }

    const char* ProviderName() const noexcept override { return "nvml"; }

  private:
    NvmlLibrary  lib_;
    nvmlDevice_t device_    = nullptr;
    bool         available_ = false;
    std::string  gpu_name_;
};

}  // namespace

// Declared in gpu_metrics_factory.cpp
std::unique_ptr<IGpuMetricsProvider> MakeNvmlGpuProvider() {
    auto p = std::make_unique<NvmlGpuProvider>();
    if (!p->IsAvailable()) return nullptr;
    return p;
}

}  // namespace framewatch
