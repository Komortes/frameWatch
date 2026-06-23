#include "framewatch/overlay/overlay_runtime.h"

namespace framewatch {

void OverlayRuntime::SetOverlayStatus(std::string message, int visible_frames) {
    overlay_status_text_ = std::move(message);
    overlay_status_frames_remaining_ = visible_frames;
}

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
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return false;
    }

    if (!session_.CaptureFrame(present_event.timestamp).has_value()) {
        return false;
    }

    last_snapshot_ = session_.GraphSnapshot();
    last_snapshot_.graph_label = session_.GraphLabel();
    if (overlay_status_frames_remaining_ > 0) {
        last_snapshot_.status_text = overlay_status_text_;
        --overlay_status_frames_remaining_;
        if (overlay_status_frames_remaining_ == 0) {
            overlay_status_text_.clear();
        }
    }
    const OverlayRenderActions actions = renderer_->Render(last_snapshot_, present_event);
    if (actions.toggle_benchmark) {
        ToggleBenchmark();
    }
    if (actions.export_requested) {
        ExportSession();
    }
    if (actions.reset_session) {
        ResetSession();
        return true;
    }
    has_snapshot_ = true;
    return true;
}

std::optional<OverlaySnapshot> OverlayRuntime::CopyLastSnapshot() const {
    std::lock_guard lock(mutex_);
    if (!has_snapshot_) {
        return std::nullopt;
    }
    return last_snapshot_;
}

bool OverlayRuntime::OnPresent(FrameClock::time_point timestamp) {
    PresentEvent present_event;
    present_event.timestamp = timestamp;
    return OnPresent(present_event);
}

void OverlayRuntime::StartBenchmark() {
    session_.StartBenchmark();
    SetOverlayStatus("BENCHMARK START");
}

void OverlayRuntime::StopBenchmark() {
    session_.StopBenchmark();
    SetOverlayStatus("BENCHMARK STOP");
}

void OverlayRuntime::ToggleBenchmark() {
    if (session_.IsBenchmarkRecording()) {
        StopBenchmark();
    } else {
        StartBenchmark();
    }
}

void OverlayRuntime::ResetSession() {
    session_.Reset();
    last_snapshot_ = {};
    has_snapshot_ = false;
    SetOverlayStatus("SESSION RESET");
}

bool OverlayRuntime::ExportSession() {
    const bool exported = session_.ExportPreferred(csv_export_path_, json_export_path_);
    SetOverlayStatus(exported ? "EXPORT OK" : "EXPORT FAILED");
    return exported;
}

void OverlayRuntime::SetExportPaths(std::filesystem::path csv_path,
                                    std::filesystem::path json_path) {
    csv_export_path_ = std::move(csv_path);
    json_export_path_ = std::move(json_path);
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
