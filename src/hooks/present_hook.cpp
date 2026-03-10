#include "framewatch/hooks/present_hook.h"

namespace framewatch {

namespace {

class UnsupportedPresentHook final : public PresentHook {
  public:
    HookBackend Backend() const noexcept override { return HookBackend::None; }

    HookState State() const noexcept override { return HookState::Unsupported; }

    std::string_view Description() const noexcept override {
        return "Present hook is only scaffolded; enable the Windows DX11 backend to implement detouring.";
    }

    bool Install() override { return false; }

    void Remove() noexcept override {}
};

}  // namespace

#if defined(_WIN32) && defined(FRAMEWATCH_HAS_DX11_HOOK)
std::unique_ptr<PresentHook> CreateDx11PresentHookWin();
#endif

std::unique_ptr<PresentHook> CreatePresentHook() {
#if defined(_WIN32) && defined(FRAMEWATCH_HAS_DX11_HOOK)
    return CreateDx11PresentHookWin();
#else
    return std::make_unique<UnsupportedPresentHook>();
#endif
}

}  // namespace framewatch
