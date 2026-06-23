#include "framewatch/core/session_logger.h"

#include "framewatch/exporter/csv_exporter.h"
#include "framewatch/exporter/json_exporter.h"

namespace framewatch {

SessionLogger::SessionLogger(std::size_t max_samples) : max_samples_(max_samples) {}

void SessionLogger::Append(const FrameSample& sample) {
    if (max_samples_ > 0 && samples_.size() >= max_samples_) {
        samples_.erase(samples_.begin());
    }
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
