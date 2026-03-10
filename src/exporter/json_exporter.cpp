#include "framewatch/exporter/json_exporter.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace framewatch {

bool ExportSamplesToJson(const std::vector<FrameSample>& samples,
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

    output << std::fixed << std::setprecision(6);
    output << "{\n";
    output << "  \"session\": {\n";
    output << "    \"sample_count\": " << samples.size() << "\n";
    output << "  },\n";
    output << "  \"frames\": [\n";

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const FrameSample& sample = samples[i];
        output << "    {\n";
        output << "      \"frame\": " << sample.frame_index << ",\n";
        output << "      \"timestamp_seconds\": " << sample.timestamp_seconds << ",\n";
        output << "      \"frametime_ms\": " << sample.frametime_ms << ",\n";
        output << "      \"fps\": " << sample.fps << '\n';
        output << "    }";
        if (i + 1 != samples.size()) {
            output << ',';
        }
        output << '\n';
    }

    output << "  ]\n";
    output << "}\n";

    return output.good();
}

}  // namespace framewatch
