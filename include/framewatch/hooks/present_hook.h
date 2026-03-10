#pragma once

#include <memory>
#include <string_view>

namespace framewatch {

enum class HookBackend {
    None,
    Dx11,
};

enum class HookState {
    Uninitialized,
    Ready,
    Running,
    Unsupported,
    Error,
};

class PresentHook {
  public:
    virtual ~PresentHook() = default;

    virtual HookBackend Backend() const noexcept = 0;
    virtual HookState State() const noexcept = 0;
    virtual std::string_view Description() const noexcept = 0;

    virtual bool Install() = 0;
    virtual void Remove() noexcept = 0;
};

std::unique_ptr<PresentHook> CreatePresentHook();

}  // namespace framewatch
