#pragma once

#include <memory>

#include "framewatch/overlay/overlay_renderer.h"
#include "framewatch/session/performance_session.h"

namespace framewatch {

class OverlayRuntime {
  public:
    explicit OverlayRuntime(std::unique_ptr<OverlayRenderer> renderer,
                            std::size_t live_history_limit = 360,
                            std::size_t benchmark_history_limit = 5'000);

    bool Initialize();
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept;

    bool OnPresent(const PresentEvent& present_event);
    bool OnPresent(FrameClock::time_point timestamp = FrameClock::now());

    void StartBenchmark();
    void StopBenchmark();
    void ToggleBenchmark();

    PerformanceSession& Session() noexcept;
    const PerformanceSession& Session() const noexcept;

    const OverlaySnapshot* LastSnapshot() const noexcept;
    const char* RendererName() const noexcept;
    std::string_view RendererDescription() const noexcept;

  private:
    std::unique_ptr<OverlayRenderer> renderer_;
    PerformanceSession session_;
    OverlaySnapshot last_snapshot_;
    bool initialized_{false};
    bool has_snapshot_{false};
};

}  // namespace framewatch
