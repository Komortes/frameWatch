#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "framewatch/core/frame_sample.h"

namespace framewatch {

class SessionLogger {
  public:
    explicit SessionLogger(std::size_t max_samples = 0);

    void Append(const FrameSample& sample);
    void Clear() noexcept;

    std::size_t Size() const noexcept;
    const std::vector<FrameSample>& Samples() const noexcept;

    bool ExportCsv(const std::filesystem::path& path) const;
    bool ExportJson(const std::filesystem::path& path) const;

  private:
    std::size_t max_samples_{0};
    std::vector<FrameSample> samples_;
};

}  // namespace framewatch
