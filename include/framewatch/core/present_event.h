#pragma once

#include <cstdint>

#include "framewatch/core/frametime_tracker.h"

namespace framewatch {

enum class GraphicsApi {
    Unknown,
    Dx11,
};

struct PresentEvent {
    GraphicsApi api{GraphicsApi::Unknown};
    FrameClock::time_point timestamp{};
    void* native_swap_chain{nullptr};
    std::uint32_t sync_interval{0};
    std::uint32_t present_flags{0};
};

}  // namespace framewatch
