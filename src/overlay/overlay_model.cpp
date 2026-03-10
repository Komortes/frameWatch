#include "framewatch/overlay/overlay_model.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace framewatch {

namespace {

std::string FormatNumber(double value, int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

}  // namespace

OverlaySnapshot OverlayModel::Build(const MetricsSnapshot& metrics,
                                    std::span<const double> frametime_history) const {
    OverlaySnapshot snapshot;

    snapshot.stats.push_back({"FPS", FormatNumber(metrics.current_fps, 1)});
    snapshot.stats.push_back({"Average FPS", FormatNumber(metrics.average_fps, 1)});
    snapshot.stats.push_back({"1% low", FormatNumber(metrics.one_percent_low_fps, 1)});
    snapshot.stats.push_back({"0.1% low",
                              FormatNumber(metrics.point_one_percent_low_fps, 1)});
    snapshot.stats.push_back({"Frametime", FormatNumber(metrics.latest_frametime_ms, 2) + " ms"});
    snapshot.stats.push_back({"Variance",
                              FormatNumber(metrics.frametime_variance_ms2, 3) + " ms^2"});

    if (frametime_history.empty()) {
        return snapshot;
    }

    const auto [min_it, max_it] =
        std::minmax_element(frametime_history.begin(), frametime_history.end());
    const double min_ms = *min_it;
    const double max_ms = *max_it;
    const double padded_min = std::max(0.0, min_ms * 0.95);
    const double padded_max = std::max(padded_min + 0.001, max_ms * 1.05);
    const double range = padded_max - padded_min;

    snapshot.graph_min_ms = padded_min;
    snapshot.graph_max_ms = padded_max;
    snapshot.graph.reserve(frametime_history.size());

    const double width_denominator =
        frametime_history.size() > 1 ? static_cast<double>(frametime_history.size() - 1) : 1.0;

    for (std::size_t i = 0; i < frametime_history.size(); ++i) {
        const double frametime_ms = frametime_history[i];
        const double normalized = std::clamp((frametime_ms - padded_min) / range, 0.0, 1.0);

        OverlayGraphPoint point;
        point.x = static_cast<double>(i) / width_denominator;
        point.y = normalized;
        point.frametime_ms = frametime_ms;
        snapshot.graph.push_back(point);
    }

    return snapshot;
}

}  // namespace framewatch
