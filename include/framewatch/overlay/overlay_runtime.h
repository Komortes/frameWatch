#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

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
    void ResetSession();
    bool ExportSession();
    void SetExportPaths(std::filesystem::path csv_path, std::filesystem::path json_path);

    PerformanceSession& Session() noexcept;
    const PerformanceSession& Session() const noexcept;

    // Safe for same-thread callers (renderer callbacks inside OnPresent).
    const OverlaySnapshot* LastSnapshot() const noexcept;
    // Thread-safe copy for cross-thread readers (e.g. status reporters).
    std::optional<OverlaySnapshot> CopyLastSnapshot() const;
    const char* RendererName() const noexcept;
    std::string_view RendererDescription() const noexcept;

  private:
    void SetOverlayStatus(std::string message, int visible_frames = 180);

    mutable std::mutex mutex_;
    std::unique_ptr<OverlayRenderer> renderer_;
    PerformanceSession session_;
    OverlaySnapshot last_snapshot_;
    std::filesystem::path csv_export_path_{"output/framewatch_overlay_hotkey.csv"};
    std::filesystem::path json_export_path_{"output/framewatch_overlay_hotkey.json"};
    std::string overlay_status_text_;
    int overlay_status_frames_remaining_{0};
    bool initialized_{false};
    bool has_snapshot_{false};
};

}  // namespace framewatch
