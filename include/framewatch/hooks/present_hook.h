#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "framewatch/core/present_event.h"

namespace framewatch {

enum class HookBackend {
    None,
    Dx11,
    Vulkan,
};

enum class HookState {
    Uninitialized,
    Ready,
    Running,
    Unsupported,
    Error,
};

using PresentCallback = std::function<void(const PresentEvent&)>;

class PresentHook {
  public:
    virtual ~PresentHook() = default;

    virtual HookBackend Backend() const noexcept = 0;
    virtual HookState State() const noexcept = 0;
    virtual std::string_view Description() const noexcept = 0;

    virtual void SetPresentCallback(PresentCallback callback) = 0;
    virtual bool Install() = 0;
    virtual void Remove() noexcept = 0;
};

std::unique_ptr<PresentHook> CreatePresentHook();

}  // namespace framewatch
