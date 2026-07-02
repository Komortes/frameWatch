"""
FrameWatch Python FFI demo — uses ctypes to call the public C API.

No pip dependencies required; only Python 3.8+ stdlib.

Usage:
    # Build the shared library first:
    #   cmake --build build --target framewatch_c_api --parallel
    #
    # Then run (library is auto-discovered under build/):
    python samples/python/framewatch_ctypes.py

    # Or point at a specific library:
    python samples/python/framewatch_ctypes.py /path/to/libframewatch_c_api.so
"""

from __future__ import annotations

import ctypes
import math
import pathlib
import platform
import sys
import time


# ---------------------------------------------------------------------------
# Library discovery
# ---------------------------------------------------------------------------

def _find_library(hint: str | None) -> pathlib.Path:
    if hint:
        p = pathlib.Path(hint)
        if not p.exists():
            raise FileNotFoundError(f"Library not found: {p}")
        return p

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    system = platform.system()

    candidates: list[pathlib.Path] = []
    if system == "Windows":
        for cfg in ("Release", "Debug", "RelWithDebInfo", ""):
            candidates.append(repo_root / "build" / cfg / "framewatch_c_api.dll")
    elif system == "Darwin":
        candidates.append(repo_root / "build" / "libframewatch_c_api.dylib")
    else:
        candidates.append(repo_root / "build" / "libframewatch_c_api.so")

    for p in candidates:
        if p.exists():
            return p

    raise FileNotFoundError(
        "Could not find framewatch_c_api shared library.\n"
        "Build it first:  cmake --build build --target framewatch_c_api --parallel\n"
        f"Searched: {[str(c) for c in candidates]}"
    )


# ---------------------------------------------------------------------------
# C API bindings
# ---------------------------------------------------------------------------

class FwSnapshot(ctypes.Structure):
    _fields_ = [
        ("sample_count",               ctypes.c_size_t),
        ("current_fps",                ctypes.c_double),
        ("average_fps",                ctypes.c_double),
        ("one_percent_low_fps",        ctypes.c_double),
        ("point_one_percent_low_fps",  ctypes.c_double),
        ("latest_frametime_ms",        ctypes.c_double),
        ("average_frametime_ms",       ctypes.c_double),
        ("min_frametime_ms",           ctypes.c_double),
        ("max_frametime_ms",           ctypes.c_double),
    ]


def _bind(lib: ctypes.CDLL) -> None:
    lib.fw_version.restype  = ctypes.c_char_p
    lib.fw_version.argtypes = []

    lib.fw_session_create.restype  = ctypes.c_void_p
    lib.fw_session_create.argtypes = []

    lib.fw_session_destroy.restype  = None
    lib.fw_session_destroy.argtypes = [ctypes.c_void_p]

    lib.fw_session_reset.restype  = None
    lib.fw_session_reset.argtypes = [ctypes.c_void_p]

    lib.fw_session_push_frame.restype  = None
    lib.fw_session_push_frame.argtypes = [ctypes.c_void_p, ctypes.c_double]

    lib.fw_session_snapshot.restype  = FwSnapshot
    lib.fw_session_snapshot.argtypes = [ctypes.c_void_p]

    lib.fw_session_start_benchmark.restype  = None
    lib.fw_session_start_benchmark.argtypes = [ctypes.c_void_p]

    lib.fw_session_stop_benchmark.restype  = None
    lib.fw_session_stop_benchmark.argtypes = [ctypes.c_void_p]

    lib.fw_session_benchmark_snapshot.restype  = ctypes.c_int
    lib.fw_session_benchmark_snapshot.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(FwSnapshot),
    ]

    lib.fw_session_export.restype  = ctypes.c_int
    lib.fw_session_export.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]


# ---------------------------------------------------------------------------
# Pretty-print helpers
# ---------------------------------------------------------------------------

def _print_snapshot(label: str, s: FwSnapshot) -> None:
    print(f"\n  [{label}]")
    print(f"    samples       : {s.sample_count}")
    print(f"    current FPS   : {s.current_fps:.1f}")
    print(f"    average FPS   : {s.average_fps:.1f}")
    print(f"    1% low        : {s.one_percent_low_fps:.1f}")
    print(f"    0.1% low      : {s.point_one_percent_low_fps:.1f}")
    print(f"    frametime     : {s.latest_frametime_ms:.3f} ms")
    print(f"    avg frametime : {s.average_frametime_ms:.3f} ms")
    print(f"    min/max ft    : {s.min_frametime_ms:.3f} / {s.max_frametime_ms:.3f} ms")


# ---------------------------------------------------------------------------
# Demo
# ---------------------------------------------------------------------------

def _simulate_frametimes(base_ms: float, jitter_ms: float, count: int) -> list[float]:
    """Sine-wave jitter around a base frametime — no random dependency."""
    return [
        max(0.1, base_ms + jitter_ms * math.sin(i * 0.3))
        for i in range(count)
    ]


def main(lib_hint: str | None = None) -> int:
    lib_path = _find_library(lib_hint)
    lib = ctypes.CDLL(str(lib_path))
    _bind(lib)

    version = lib.fw_version().decode()
    print(f"FrameWatch C API  v{version}  ({lib_path.name})")

    session = lib.fw_session_create()
    if not session:
        print("ERROR: fw_session_create returned NULL", file=sys.stderr)
        return 1

    try:
        # ── Live rolling window demo ──────────────────────────────────────
        print("\n=== Live rolling window (300 frames @ ~60 fps) ===")
        frames_60fps = _simulate_frametimes(base_ms=16.667, jitter_ms=2.0, count=300)
        for ft in frames_60fps:
            lib.fw_session_push_frame(session, ft)

        snap = lib.fw_session_snapshot(session)
        _print_snapshot("60 fps steady", snap)

        # ── Benchmark recording ───────────────────────────────────────────
        print("\n=== Benchmark recording (100 frames @ ~30 fps with spikes) ===")
        lib.fw_session_reset(session)
        lib.fw_session_start_benchmark(session)

        frames_30fps = _simulate_frametimes(base_ms=33.333, jitter_ms=8.0, count=100)
        # inject two heavy spikes
        frames_30fps[25] = 80.0
        frames_30fps[75] = 95.0
        for ft in frames_30fps:
            lib.fw_session_push_frame(session, ft)

        lib.fw_session_stop_benchmark(session)

        bmark = FwSnapshot()
        if lib.fw_session_benchmark_snapshot(session, ctypes.byref(bmark)):
            _print_snapshot("30 fps + spikes", bmark)
        else:
            print("  (no benchmark data)")

        # ── Export ────────────────────────────────────────────────────────
        out_dir = pathlib.Path("output")
        out_dir.mkdir(exist_ok=True)
        csv_path  = str(out_dir / "python_demo.csv").encode()
        json_path = str(out_dir / "python_demo.json").encode()

        rc = lib.fw_session_export(session, csv_path, json_path)
        print(f"\n=== Export ===")
        if rc == 0:
            print(f"  CSV  → {out_dir / 'python_demo.csv'}")
            print(f"  JSON → {out_dir / 'python_demo.json'}")
        else:
            print("  export failed (non-zero return code)")

    finally:
        lib.fw_session_destroy(session)

    print("\nDone.")
    return 0


if __name__ == "__main__":
    hint = sys.argv[1] if len(sys.argv) > 1 else None
    sys.exit(main(hint))
