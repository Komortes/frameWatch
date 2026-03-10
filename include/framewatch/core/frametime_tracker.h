#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "framewatch/core/frame_sample.h"

namespace framewatch {

using FrameClock = std::chrono::steady_clock;

class FrametimeTracker {
  public:
    std::optional<FrameSample> Capture(FrameClock::time_point timestamp = FrameClock::now());
    void Reset() noexcept;
    std::uint64_t FrameCount() const noexcept;

  private:
    std::optional<FrameClock::time_point> session_start_;
    std::optional<FrameClock::time_point> previous_frame_;
    std::uint64_t captured_frames_{0};
};

}  // namespace framewatch
