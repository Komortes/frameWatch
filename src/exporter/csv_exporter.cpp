#include "framewatch/exporter/csv_exporter.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace framewatch {

bool ExportSamplesToCsv(const std::vector<FrameSample>& samples,
                        const std::filesystem::path& path) {
    std::error_code create_error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), create_error);
        if (create_error) {
            return false;
        }
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    output << "frame,timestamp_seconds,frametime_ms,fps\n";
    output << std::fixed << std::setprecision(6);

    for (const FrameSample& sample : samples) {
        output << sample.frame_index << ',' << sample.timestamp_seconds << ','
               << sample.frametime_ms << ',' << sample.fps << '\n';
    }

    return output.good();
}

}  // namespace framewatch
