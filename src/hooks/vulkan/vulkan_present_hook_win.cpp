#ifdef _WIN32

#include "framewatch/hooks/vulkan/vulkan_present_hook_win.h"

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#if defined(FRAMEWATCH_HAS_MINHOOK)
#include <MinHook.h>
#endif

// ---------------------------------------------------------------------------
// Minimal Vulkan ABI types — avoids a hard Vulkan SDK dependency.
// These definitions match the Vulkan 1.0+ ABI which is stable.
// ---------------------------------------------------------------------------

#define VK_DEFINE_HANDLE(T) typedef struct T##_T* T;
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkSwapchainKHR)
VK_DEFINE_HANDLE(VkSemaphore)
#undef VK_DEFINE_HANDLE

enum VkResult : int { VK_SUCCESS = 0 };

struct VkPresentInfoKHR {
    std::int32_t     sType;
    const void*      pNext;
    std::uint32_t    waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    std::uint32_t    swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const std::uint32_t*  pImageIndices;
    VkResult*        pResults;
};

using PFN_vkQueuePresentKHR = VkResult(__stdcall*)(VkQueue, const VkPresentInfoKHR*);

// ---------------------------------------------------------------------------

namespace framewatch {

std::atomic<VulkanPresentHookWin*> VulkanPresentHookWin::active_instance_{nullptr};
void* VulkanPresentHookWin::original_present_{nullptr};

VulkanPresentHookWin::VulkanPresentHookWin() = default;

VulkanPresentHookWin::~VulkanPresentHookWin() {
    Remove();
}

HookState VulkanPresentHookWin::State() const noexcept {
    return state_.load(std::memory_order_relaxed);
}

std::string_view VulkanPresentHookWin::Description() const noexcept {
    if (!last_error_.empty()) return last_error_;
#if defined(FRAMEWATCH_HAS_MINHOOK)
    return "Vulkan vkQueuePresentKHR detour via MinHook";
#else
    return "Vulkan hook scaffold (MinHook not available)";
#endif
}

void VulkanPresentHookWin::SetPresentCallback(PresentCallback callback) {
    callback_ = std::move(callback);
}

bool VulkanPresentHookWin::Install() {
#if !defined(FRAMEWATCH_HAS_MINHOOK)
    last_error_ = "MinHook not available — Vulkan hook cannot be installed";
    state_.store(HookState::Unsupported, std::memory_order_relaxed);
    return false;
#else
    VulkanPresentHookWin* expected = nullptr;
    if (!active_instance_.compare_exchange_strong(expected, this,
                                                   std::memory_order_acq_rel)) {
        last_error_ = "Another VulkanPresentHookWin instance is already active";
        state_.store(HookState::Error, std::memory_order_relaxed);
        return false;
    }

    // Ensure vulkan-1.dll is loaded (it may not be present on the system).
    if (GetModuleHandleW(L"vulkan-1.dll") == nullptr) {
        if (LoadLibraryW(L"vulkan-1.dll") == nullptr) {
            last_error_ = "vulkan-1.dll not found — Vulkan not installed on this system";
            state_.store(HookState::Unsupported, std::memory_order_relaxed);
            active_instance_.store(nullptr, std::memory_order_release);
            return false;
        }
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        last_error_ = std::string("MH_Initialize failed: ") + std::to_string(status);
        state_.store(HookState::Error, std::memory_order_relaxed);
        active_instance_.store(nullptr, std::memory_order_release);
        return false;
    }

    void* trampoline = nullptr;
    status = MH_CreateHookApi(L"vulkan-1.dll", "vkQueuePresentKHR",
                              reinterpret_cast<void*>(&HookQueuePresent),
                              &original_present_);
    if (status != MH_OK) {
        last_error_ = std::string("MH_CreateHookApi(vkQueuePresentKHR) failed: ") +
                      std::to_string(status);
        state_.store(HookState::Error, std::memory_order_relaxed);
        active_instance_.store(nullptr, std::memory_order_release);
        return false;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        MH_RemoveHook(MH_ALL_HOOKS);
        original_present_ = nullptr;
        last_error_ = std::string("MH_EnableHook failed: ") + std::to_string(status);
        state_.store(HookState::Error, std::memory_order_relaxed);
        active_instance_.store(nullptr, std::memory_order_release);
        return false;
    }

    last_error_.clear();
    state_.store(HookState::Running, std::memory_order_relaxed);
    return true;
#endif
}

void VulkanPresentHookWin::Remove() noexcept {
#if defined(FRAMEWATCH_HAS_MINHOOK)
    if (state_.load(std::memory_order_relaxed) != HookState::Running) {
        active_instance_.compare_exchange_strong(
            reinterpret_cast<VulkanPresentHookWin*&>(
                *const_cast<VulkanPresentHookWin**>(
                    &reinterpret_cast<VulkanPresentHookWin* volatile&>(active_instance_))),
            nullptr, std::memory_order_acq_rel);
        return;
    }
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    original_present_ = nullptr;
#endif

    state_.store(HookState::Ready, std::memory_order_relaxed);
    VulkanPresentHookWin* self = this;
    active_instance_.compare_exchange_strong(self, nullptr, std::memory_order_acq_rel);
}

// static
int __stdcall VulkanPresentHookWin::HookQueuePresent(void* queue,
                                                      const void* present_info) {
    const auto* info = static_cast<const VkPresentInfoKHR*>(present_info);

    VulkanPresentHookWin* instance = active_instance_.load(std::memory_order_acquire);
    if (instance && instance->callback_) {
        PresentEvent event;
        event.api = GraphicsApi::Vulkan;
        event.timestamp = FrameClock::now();
        if (info && info->swapchainCount > 0 && info->pSwapchains) {
            event.native_swap_chain = static_cast<void*>(info->pSwapchains[0]);
        }
        instance->callback_(event);
    }

    auto* original = reinterpret_cast<PFN_vkQueuePresentKHR>(original_present_);
    if (original) {
        return static_cast<int>(
            original(static_cast<VkQueue>(queue),
                     static_cast<const VkPresentInfoKHR*>(present_info)));
    }
    return 0;  // VK_SUCCESS
}

}  // namespace framewatch

#endif  // _WIN32
