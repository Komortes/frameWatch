#include "framewatch/overlay/overlay_renderer.h"

#include <cstdint>
#include <memory>

namespace framewatch {

namespace {

class Dx11OverlayRendererWin final : public OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "Dx11OverlayRendererWin"; }

    std::string_view Description() const noexcept override {
        return bound_swap_chain_ != nullptr
                   ? "DX11 overlay renderer scaffold is receiving swap chain context."
                   : "DX11 overlay renderer scaffold compiled. Waiting for swap chain binding from the Present detour.";
    }

    bool Initialize() override {
        initialized_ = true;
        bound_swap_chain_ = nullptr;
        rendered_frames_ = 0;
        return true;
    }

    void Render(const OverlaySnapshot&, const PresentEvent& present_event) override {
        if (!initialized_) {
            return;
        }

        if (present_event.api == GraphicsApi::Dx11 && present_event.native_swap_chain != nullptr) {
            bound_swap_chain_ = present_event.native_swap_chain;
        }

        ++rendered_frames_;
    }

    void Shutdown() noexcept override {
        initialized_ = false;
        bound_swap_chain_ = nullptr;
        rendered_frames_ = 0;
    }

  private:
    bool initialized_{false};
    void* bound_swap_chain_{nullptr};
    std::uint64_t rendered_frames_{0};
};

}  // namespace

std::unique_ptr<OverlayRenderer> CreateDx11OverlayRendererWin() {
    return std::make_unique<Dx11OverlayRendererWin>();
}

}  // namespace framewatch
