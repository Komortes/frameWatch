#include "framewatch/core/frametime_tracker.h"

namespace framewatch {

std::optional<FrameSample> FrametimeTracker::Capture(FrameClock::time_point timestamp) {
    if (!session_start_.has_value()) {
        session_start_ = timestamp;
        previous_frame_ = timestamp;
        return std::nullopt;
    }

    if (!previous_frame_.has_value() || timestamp <= *previous_frame_) {
        previous_frame_ = timestamp;
        return std::nullopt;
    }

    const auto frametime = timestamp - *previous_frame_;
    const auto session_elapsed = timestamp - *session_start_;

    previous_frame_ = timestamp;
    ++captured_frames_;

    const double frametime_ms =
        std::chrono::duration<double, std::milli>(frametime).count();

    FrameSample sample;
    sample.frame_index = captured_frames_;
    sample.timestamp_seconds = std::chrono::duration<double>(session_elapsed).count();
    sample.frametime_ms = frametime_ms;
    sample.fps = frametime_ms > 0.0 ? (1'000.0 / frametime_ms) : 0.0;
    return sample;
}

void FrametimeTracker::Reset() noexcept {
    session_start_.reset();
    previous_frame_.reset();
    captured_frames_ = 0;
}

std::uint64_t FrametimeTracker::FrameCount() const noexcept {
    return captured_frames_;
}

}  // namespace framewatch
