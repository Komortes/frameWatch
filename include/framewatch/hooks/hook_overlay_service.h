#pragma once

#include <memory>
#include <string_view>

#include "framewatch/hooks/present_hook.h"
#include "framewatch/overlay/overlay_runtime.h"

namespace framewatch {

class HookOverlayService {
  public:
    HookOverlayService(std::unique_ptr<PresentHook> hook,
                       std::unique_ptr<OverlayRenderer> renderer,
                       std::size_t live_history_limit = 360,
                       std::size_t benchmark_history_limit = 5'000);

    bool Initialize();
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept;

    HookBackend HookBackendType() const noexcept;
    HookState HookStatus() const noexcept;
    std::string_view HookDescription() const noexcept;

    OverlayRuntime& Runtime() noexcept;
    const OverlayRuntime& Runtime() const noexcept;

  private:
    std::unique_ptr<PresentHook> hook_;
    OverlayRuntime runtime_;
    bool initialized_{false};
};

std::unique_ptr<HookOverlayService> CreateHookOverlayService(
    std::size_t live_history_limit = 360,
    std::size_t benchmark_history_limit = 5'000);

}  // namespace framewatch
