#pragma once

#include <memory>
#include <string_view>

#include "framewatch/core/present_event.h"
#include "framewatch/overlay/overlay_model.h"

namespace framewatch {

struct OverlayRenderActions {
    bool toggle_benchmark{false};
    bool export_requested{false};
    bool reset_session{false};
};

class OverlayRenderer {
  public:
    virtual ~OverlayRenderer() = default;

    virtual const char* Name() const noexcept = 0;
    virtual std::string_view Description() const noexcept = 0;
    virtual bool Initialize() = 0;
    virtual OverlayRenderActions Render(const OverlaySnapshot& snapshot,
                                        const PresentEvent& present_event) = 0;
    virtual void Shutdown() noexcept = 0;
};

std::unique_ptr<OverlayRenderer> CreateOverlayRenderer();

}  // namespace framewatch
