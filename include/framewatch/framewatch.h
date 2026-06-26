/**
 * FrameWatch public C API — stable ABI, safe for FFI (Python, Rust, Go, etc.)
 *
 * Thread-safety: each fw_session_t is single-threaded; don't share handles.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* ── Version ──────────────────────────────────────────────────────────────── */

/** Returns "major.minor.patch" string literal. */
const char* fw_version(void);

/* ── Session handle ───────────────────────────────────────────────────────── */

typedef struct fw_session_s* fw_session_t;

fw_session_t fw_session_create(void);
void         fw_session_destroy(fw_session_t session);
void         fw_session_reset(fw_session_t session);

/* ── Frame capture ────────────────────────────────────────────────────────── */

/**
 * Push one frametime sample (milliseconds).
 * Call this once per present event.
 */
void fw_session_push_frame(fw_session_t session, double frametime_ms);

/* ── Metrics snapshot ─────────────────────────────────────────────────────── */

typedef struct {
    size_t sample_count;
    double current_fps;
    double average_fps;
    double one_percent_low_fps;
    double point_one_percent_low_fps;
    double latest_frametime_ms;
    double average_frametime_ms;
    double min_frametime_ms;
    double max_frametime_ms;
} fw_snapshot_t;

/** Returns a snapshot of the live rolling window. */
fw_snapshot_t fw_session_snapshot(fw_session_t session);

/* ── Benchmark recording ──────────────────────────────────────────────────── */

void fw_session_start_benchmark(fw_session_t session);
void fw_session_stop_benchmark(fw_session_t session);

/**
 * Returns 1 if a completed benchmark snapshot was written to *out, 0 if no
 * benchmark has been run yet.
 */
int fw_session_benchmark_snapshot(fw_session_t session, fw_snapshot_t* out);

/* ── Export ───────────────────────────────────────────────────────────────── */

/**
 * Export the live session to CSV + JSON files.
 * Returns 0 on success, -1 on error (null paths, write failure).
 */
int fw_session_export(fw_session_t session,
                      const char* csv_path,
                      const char* json_path);

#ifdef __cplusplus
}
#endif
