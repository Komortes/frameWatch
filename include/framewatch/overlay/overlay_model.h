#pragma once

#include <span>
#include <string>
#include <vector>

#include "framewatch/core/metrics_engine.h"

namespace framewatch {

struct OverlayStatLine {
    std::string label;
    std::string value;
};

struct OverlayGraphPoint {
    double x{0.0};
    double y{0.0};
    double frametime_ms{0.0};
};

struct OverlaySnapshot {
    std::vector<OverlayStatLine> stats;
    std::vector<OverlayGraphPoint> graph;
    double graph_min_ms{0.0};
    double graph_max_ms{0.0};
    std::string graph_label;
};

class OverlayModel {
  public:
    OverlaySnapshot Build(const MetricsSnapshot& metrics,
                          std::span<const double> frametime_history) const;
};

}  // namespace framewatch
