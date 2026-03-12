#include "framewatch/hooks/present_hook.h"

#include <memory>

namespace framewatch {

namespace {

class Dx11PresentHookWin final : public PresentHook {
  public:
    HookBackend Backend() const noexcept override { return HookBackend::Dx11; }

    HookState State() const noexcept override { return state_; }

    std::string_view Description() const noexcept override {
        return "DX11 hook scaffold compiled. Replace this stub with a MinHook Present detour.";
    }

    void SetPresentCallback(PresentCallback callback) override { callback_ = std::move(callback); }

    bool Install() override {
        state_ = HookState::Error;
        return false;
    }

    void Remove() noexcept override { state_ = HookState::Ready; }

  private:
    HookState state_{HookState::Ready};
    PresentCallback callback_;
};

}  // namespace

std::unique_ptr<PresentHook> CreateDx11PresentHookWin() {
    return std::make_unique<Dx11PresentHookWin>();
}

}  // namespace framewatch
