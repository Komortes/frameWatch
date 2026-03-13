#include "framewatch/hooks/hook_overlay_service.h"

namespace framewatch {

HookOverlayService::HookOverlayService(std::unique_ptr<PresentHook> hook,
                                       std::unique_ptr<OverlayRenderer> renderer,
                                       std::size_t live_history_limit,
                                       std::size_t benchmark_history_limit)
    : hook_(std::move(hook)),
      runtime_(std::move(renderer), live_history_limit, benchmark_history_limit) {}

bool HookOverlayService::Initialize() {
    if (initialized_) {
        return true;
    }

    if (!hook_) {
        return false;
    }

    if (!runtime_.Initialize()) {
        return false;
    }

    hook_->SetPresentCallback([this](const PresentEvent& present_event) {
        runtime_.OnPresent(present_event);
    });

    if (!hook_->Install()) {
        runtime_.Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void HookOverlayService::Shutdown() noexcept {
    if (hook_) {
        hook_->Remove();
    }
    runtime_.Shutdown();
    initialized_ = false;
}

bool HookOverlayService::IsInitialized() const noexcept {
    return initialized_;
}

HookBackend HookOverlayService::HookBackendType() const noexcept {
    return hook_ ? hook_->Backend() : HookBackend::None;
}

HookState HookOverlayService::HookStatus() const noexcept {
    return hook_ ? hook_->State() : HookState::Uninitialized;
}

std::string_view HookOverlayService::HookDescription() const noexcept {
    return hook_ ? hook_->Description() : "No present hook configured.";
}

OverlayRuntime& HookOverlayService::Runtime() noexcept {
    return runtime_;
}

const OverlayRuntime& HookOverlayService::Runtime() const noexcept {
    return runtime_;
}

std::unique_ptr<HookOverlayService> CreateHookOverlayService(std::size_t live_history_limit,
                                                             std::size_t benchmark_history_limit) {
    return std::make_unique<HookOverlayService>(CreatePresentHook(),
                                                CreateOverlayRenderer(),
                                                live_history_limit,
                                                benchmark_history_limit);
}

}  // namespace framewatch
