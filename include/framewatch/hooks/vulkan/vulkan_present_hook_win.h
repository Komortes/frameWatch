#pragma once
#ifdef _WIN32

#include <atomic>
#include <string>

#include "framewatch/hooks/present_hook.h"

namespace framewatch {

// Detours vkQueuePresentKHR in vulkan-1.dll via MinHook.
// Requires MinHook to be present; returns HookState::Unsupported otherwise.
// Only one instance may be active at a time (enforced via atomic CAS).
class VulkanPresentHookWin final : public PresentHook {
  public:
    VulkanPresentHookWin();
    ~VulkanPresentHookWin() override;

    VulkanPresentHookWin(const VulkanPresentHookWin&) = delete;
    VulkanPresentHookWin& operator=(const VulkanPresentHookWin&) = delete;

    HookBackend Backend() const noexcept override { return HookBackend::Vulkan; }
    HookState State() const noexcept override;
    std::string_view Description() const noexcept override;

    void SetPresentCallback(PresentCallback callback) override;
    bool Install() override;
    void Remove() noexcept override;

  private:
    // Hook trampoline — called instead of vkQueuePresentKHR.
    static int __stdcall HookQueuePresent(void* queue, const void* present_info);

    PresentCallback callback_;
    std::string last_error_;
    std::atomic<HookState> state_{HookState::Ready};

    static std::atomic<VulkanPresentHookWin*> active_instance_;
    static void* original_present_;  // PFN_vkQueuePresentKHR
};

}  // namespace framewatch

#endif  // _WIN32
