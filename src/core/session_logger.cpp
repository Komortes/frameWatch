#include "framewatch/core/session_logger.h"

#include "framewatch/exporter/csv_exporter.h"
#include "framewatch/exporter/json_exporter.h"

namespace framewatch {

void SessionLogger::Append(const FrameSample& sample) {
    samples_.push_back(sample);
}

void SessionLogger::Clear() noexcept {
    samples_.clear();
}

std::size_t SessionLogger::Size() const noexcept {
    return samples_.size();
}

const std::vector<FrameSample>& SessionLogger::Samples() const noexcept {
    return samples_;
}

bool SessionLogger::ExportCsv(const std::filesystem::path& path) const {
    return ExportSamplesToCsv(samples_, path);
}

bool SessionLogger::ExportJson(const std::filesystem::path& path) const {
    return ExportSamplesToJson(samples_, path);
}

}  // namespace framewatch
