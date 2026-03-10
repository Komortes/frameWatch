#pragma once

#include <cstdint>

namespace framewatch {

struct FrameSample {
    std::uint64_t frame_index{0};
    double timestamp_seconds{0.0};
    double frametime_ms{0.0};
    double fps{0.0};
};

}  // namespace framewatch
