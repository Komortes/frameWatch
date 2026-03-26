#include "framewatch/overlay/overlay_renderer.h"

#include <memory>

namespace framewatch {

namespace {

class NullOverlayRenderer final : public OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "NullOverlayRenderer"; }

    std::string_view Description() const noexcept override {
        return "Null overlay renderer compiled. Use the SDL debug window until a native overlay backend is bound.";
    }

    bool Initialize() override { return true; }

    OverlayRenderActions Render(const OverlaySnapshot&, const PresentEvent&) override { return {}; }

    void Shutdown() noexcept override {}
};

}  // namespace

#if defined(_WIN32) && defined(FRAMEWATCH_HAS_DX11_OVERLAY)
std::unique_ptr<OverlayRenderer> CreateDx11OverlayRendererWin();
#endif

std::unique_ptr<OverlayRenderer> CreateOverlayRenderer() {
#if defined(_WIN32) && defined(FRAMEWATCH_HAS_DX11_OVERLAY)
    return CreateDx11OverlayRendererWin();
#else
    return std::make_unique<NullOverlayRenderer>();
#endif
}

}  // namespace framewatch
