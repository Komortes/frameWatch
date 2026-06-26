#include "framewatch/framewatch.h"

#include <cstring>
#include <memory>

#include "framewatch/session/performance_session.h"

#define FW_VERSION_STRING "0.1.0"

extern "C" {

/* ── Version ──────────────────────────────────────────────────────────────── */

const char* fw_version(void) {
    return FW_VERSION_STRING;
}

/* ── Session ──────────────────────────────────────────────────────────────── */

struct fw_session_s {
    framewatch::PerformanceSession session;
    fw_session_s() : session() {}
};

fw_session_t fw_session_create(void) {
    return new (std::nothrow) fw_session_s();
}

void fw_session_destroy(fw_session_t s) {
    delete s;
}

void fw_session_reset(fw_session_t s) {
    if (!s) return;
    s->session.Reset();
}

/* ── Frame capture ────────────────────────────────────────────────────────── */

void fw_session_push_frame(fw_session_t s, double frametime_ms) {
    if (!s) return;
    s->session.CaptureSyntheticFrame(frametime_ms);
}

/* ── Snapshot ─────────────────────────────────────────────────────────────── */

static fw_snapshot_t to_c_snapshot(const framewatch::MetricsSnapshot& m) {
    fw_snapshot_t out{};
    out.sample_count             = m.sample_count;
    out.current_fps              = m.current_fps;
    out.average_fps              = m.average_fps;
    out.one_percent_low_fps      = m.one_percent_low_fps;
    out.point_one_percent_low_fps = m.point_one_percent_low_fps;
    out.latest_frametime_ms      = m.latest_frametime_ms;
    out.average_frametime_ms     = m.average_frametime_ms;
    out.min_frametime_ms         = m.min_frametime_ms;
    out.max_frametime_ms         = m.max_frametime_ms;
    return out;
}

fw_snapshot_t fw_session_snapshot(fw_session_t s) {
    if (!s) return fw_snapshot_t{};
    return to_c_snapshot(s->session.LiveMetrics());
}

/* ── Benchmark ────────────────────────────────────────────────────────────── */

void fw_session_start_benchmark(fw_session_t s) {
    if (!s) return;
    s->session.StartBenchmark();
}

void fw_session_stop_benchmark(fw_session_t s) {
    if (!s) return;
    s->session.StopBenchmark();
}

int fw_session_benchmark_snapshot(fw_session_t s, fw_snapshot_t* out) {
    if (!s || !out) return 0;
    const framewatch::BenchmarkSummary bm = s->session.CurrentBenchmark();
    if (!bm.has_data) return 0;
    *out = to_c_snapshot(bm.metrics);
    return 1;
}

/* ── Export ───────────────────────────────────────────────────────────────── */

int fw_session_export(fw_session_t s,
                      const char* csv_path,
                      const char* json_path) {
    if (!s || !csv_path || !json_path) return -1;
    const bool ok = s->session.ExportPreferred(
        std::filesystem::path{csv_path},
        std::filesystem::path{json_path});
    return ok ? 0 : -1;
}

}  // extern "C"
