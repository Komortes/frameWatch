#pragma once

#include <memory>

#include "framewatch/overlay/overlay_model.h"

namespace framewatch {

class OverlayRenderer {
  public:
    virtual ~OverlayRenderer() = default;

    virtual const char* Name() const noexcept = 0;
    virtual bool Initialize() = 0;
    virtual void Render(const OverlaySnapshot& snapshot) = 0;
    virtual void Shutdown() noexcept = 0;
};

std::unique_ptr<OverlayRenderer> CreateOverlayRenderer();

}  // namespace framewatch
