#pragma once

#include <filesystem>
#include <vector>

#include "framewatch/core/frame_sample.h"

namespace framewatch {

bool ExportSamplesToJson(const std::vector<FrameSample>& samples,
                         const std::filesystem::path& path);

}  // namespace framewatch
