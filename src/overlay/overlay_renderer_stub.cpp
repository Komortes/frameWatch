#include "framewatch/overlay/overlay_renderer.h"

#include <memory>

namespace framewatch {

namespace {

class NullOverlayRenderer final : public OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "NullOverlayRenderer"; }

    bool Initialize() override { return true; }

    void Render(const OverlaySnapshot&) override {}

    void Shutdown() noexcept override {}
};

}  // namespace

std::unique_ptr<OverlayRenderer> CreateOverlayRenderer() {
    return std::make_unique<NullOverlayRenderer>();
}

}  // namespace framewatch
