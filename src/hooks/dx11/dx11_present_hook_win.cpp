#include "framewatch/hooks/present_hook.h"

#include <memory>

namespace framewatch {

namespace {

class Dx11PresentHookWin final : public PresentHook {
  public:
    HookBackend Backend() const noexcept override { return HookBackend::Dx11; }

    HookState State() const noexcept override { return state_; }

    std::string_view Description() const noexcept override {
        return "DX11 hook scaffold compiled. Wire the real Present detour into DispatchPresent(...).";
    }

    void SetPresentCallback(PresentCallback callback) override { callback_ = std::move(callback); }

    bool Install() override {
        state_ = HookState::Error;
        return false;
    }

    void Remove() noexcept override { state_ = HookState::Ready; }

  private:
    void DispatchPresent(void* native_swap_chain,
                         std::uint32_t sync_interval,
                         std::uint32_t present_flags) {
        if (!callback_) {
            return;
        }

        PresentEvent present_event;
        present_event.api = GraphicsApi::Dx11;
        present_event.timestamp = FrameClock::now();
        present_event.native_swap_chain = native_swap_chain;
        present_event.sync_interval = sync_interval;
        present_event.present_flags = present_flags;
        callback_(present_event);
    }

    HookState state_{HookState::Ready};
    PresentCallback callback_;
};

}  // namespace

std::unique_ptr<PresentHook> CreateDx11PresentHookWin() {
    return std::make_unique<Dx11PresentHookWin>();
}

}  // namespace framewatch
