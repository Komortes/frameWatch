#include "framewatch/overlay/overlay_runtime.h"

namespace framewatch {

OverlayRuntime::OverlayRuntime(std::unique_ptr<OverlayRenderer> renderer,
                               std::size_t live_history_limit,
                               std::size_t benchmark_history_limit)
    : renderer_(std::move(renderer)),
      session_(live_history_limit, benchmark_history_limit) {}

bool OverlayRuntime::Initialize() {
    if (!renderer_) {
        return false;
    }

    initialized_ = renderer_->Initialize();
    return initialized_;
}

void OverlayRuntime::Shutdown() noexcept {
    if (renderer_) {
        renderer_->Shutdown();
    }
    initialized_ = false;
}

bool OverlayRuntime::IsInitialized() const noexcept {
    return initialized_;
}

bool OverlayRuntime::OnPresent(const PresentEvent& present_event) {
    if (!initialized_) {
        return false;
    }

    if (!session_.CaptureFrame(present_event.timestamp).has_value()) {
        return false;
    }

    last_snapshot_ = session_.GraphSnapshot();
    renderer_->Render(last_snapshot_, present_event);
    has_snapshot_ = true;
    return true;
}

bool OverlayRuntime::OnPresent(FrameClock::time_point timestamp) {
    PresentEvent present_event;
    present_event.timestamp = timestamp;
    return OnPresent(present_event);
}

void OverlayRuntime::StartBenchmark() {
    session_.StartBenchmark();
}

void OverlayRuntime::StopBenchmark() {
    session_.StopBenchmark();
}

void OverlayRuntime::ToggleBenchmark() {
    session_.ToggleBenchmark();
}

PerformanceSession& OverlayRuntime::Session() noexcept {
    return session_;
}

const PerformanceSession& OverlayRuntime::Session() const noexcept {
    return session_;
}

const OverlaySnapshot* OverlayRuntime::LastSnapshot() const noexcept {
    return has_snapshot_ ? &last_snapshot_ : nullptr;
}

const char* OverlayRuntime::RendererName() const noexcept {
    return renderer_ ? renderer_->Name() : "NoOverlayRenderer";
}

std::string_view OverlayRuntime::RendererDescription() const noexcept {
    return renderer_ ? renderer_->Description() : "No overlay renderer configured.";
}

}  // namespace framewatch
