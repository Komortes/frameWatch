#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "framewatch/hooks/hook_overlay_service.h"
#include "framewatch/overlay/overlay_model.h"

namespace {

struct DemoOptions {
    int frames{600};
    std::filesystem::path csv_path{"output/framewatch_session.csv"};
    std::filesystem::path json_path{"output/framewatch_session.json"};
};

bool ParseInteger(std::string_view value, int& output) {
    try {
        const int parsed = std::stoi(std::string(value));
        if (parsed <= 0) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

DemoOptions ParseArgs(int argc, char** argv) {
    DemoOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && (i + 1) < argc) {
            int parsed_frames = options.frames;
            if (ParseInteger(argv[++i], parsed_frames)) {
                options.frames = parsed_frames;
            }
        } else if (arg == "--csv" && (i + 1) < argc) {
            options.csv_path = argv[++i];
        } else if (arg == "--json" && (i + 1) < argc) {
            options.json_path = argv[++i];
        }
    }

    return options;
}

const char* HookBackendName(framewatch::HookBackend backend) {
    switch (backend) {
        case framewatch::HookBackend::Dx11:
            return "DX11";
        case framewatch::HookBackend::None:
        default:
            return "None";
    }
}

void PrintMetrics(const framewatch::MetricsSnapshot& snapshot) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "FPS: " << snapshot.current_fps << '\n';
    std::cout << "Average FPS: " << snapshot.average_fps << '\n';
    std::cout << "1% low: " << snapshot.one_percent_low_fps << '\n';
    std::cout << "0.1% low: " << snapshot.point_one_percent_low_fps << '\n';
    std::cout << "Frametime: " << snapshot.latest_frametime_ms << " ms\n";
    std::cout << "Variance: " << snapshot.frametime_variance_ms2 << " ms^2\n";
}

void PrintAsciiGraph(std::span<const double> frametimes) {
    if (frametimes.empty()) {
        return;
    }

    constexpr int graph_width = 48;
    constexpr int graph_height = 10;
    const std::size_t start_index =
        frametimes.size() > static_cast<std::size_t>(graph_width)
            ? frametimes.size() - static_cast<std::size_t>(graph_width)
            : 0;

    std::vector<double> visible(frametimes.begin() + static_cast<std::ptrdiff_t>(start_index),
                                frametimes.end());

    const auto [min_it, max_it] =
        std::minmax_element(visible.begin(), visible.end());
    const double min_value = *min_it;
    const double max_value = *max_it;
    const double range = std::max(0.001, max_value - min_value);

    std::array<std::string, graph_height> rows;
    for (std::string& row : rows) {
        row.assign(visible.size(), ' ');
    }

    for (std::size_t x = 0; x < visible.size(); ++x) {
        const double normalized = (visible[x] - min_value) / range;
        const int y = static_cast<int>(std::lround(normalized * (graph_height - 1)));
        rows[graph_height - 1 - y][x] = '*';
    }

    std::cout << "\nFrametime graph (last " << visible.size() << " frames)\n";
    for (int row = 0; row < graph_height; ++row) {
        const double label =
            max_value - ((max_value - min_value) * static_cast<double>(row) / (graph_height - 1));
        std::cout << std::setw(6) << std::setprecision(2) << label << " | " << rows[row] << '\n';
    }
    std::cout << "        +" << std::string(visible.size() + 2, '-') << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const DemoOptions options = ParseArgs(argc, argv);

    std::unique_ptr<framewatch::HookOverlayService> service =
        framewatch::CreateHookOverlayService(300);
    framewatch::PerformanceSession& session = service->Runtime().Session();

    std::mt19937 rng(42);
    std::normal_distribution<double> baseline_frametime_ms(16.6, 0.35);

    session.ResetSyntheticTimeline();

    for (int i = 0; i < options.frames; ++i) {
        double frametime_ms = std::max(5.0, baseline_frametime_ms(rng));

        if ((i + 1) % 120 == 0) {
            frametime_ms += 8.5;
        }

        if ((i + 1) % 257 == 0) {
            frametime_ms += 16.0;
        }

        session.CaptureSyntheticFrame(frametime_ms);
    }

    const framewatch::MetricsSnapshot snapshot = session.LiveMetrics();
    const framewatch::OverlaySnapshot overlay_snapshot = session.GraphSnapshot();
    std::vector<double> history;
    history.reserve(overlay_snapshot.graph.size());
    for (const framewatch::OverlayGraphPoint& point : overlay_snapshot.graph) {
        history.push_back(point.frametime_ms);
    }

    const bool exported = session.ExportPreferred(options.csv_path, options.json_path);

    std::cout << "FrameWatch Mini MVP demo\n";
    std::cout << "Hook backend: " << HookBackendName(service->HookBackendType()) << '\n';
    std::cout << "Hook status: " << service->HookDescription() << '\n';
    std::cout << "Overlay renderer: " << service->Runtime().RendererName() << '\n';
    std::cout << "Renderer status: " << service->Runtime().RendererDescription() << "\n\n";

    PrintMetrics(snapshot);
    PrintAsciiGraph(history);

    std::cout << "\nSamples captured: " << session.LiveSampleCount() << '\n';
    std::cout << "CSV export: " << (exported ? "ok" : "failed")
              << " -> " << options.csv_path.string() << '\n';
    std::cout << "JSON export: " << (exported ? "ok" : "failed")
              << " -> " << options.json_path.string() << '\n';

    return exported ? EXIT_SUCCESS : EXIT_FAILURE;
}
