# Changelog

All notable changes to this project will be documented here.

## [Unreleased]

### Fixed
- MetricsEngine: replaced `E[x²]−(E[x])²` variance formula with Welford's online
  algorithm (rolling add/remove) — eliminates catastrophic cancellation on long sessions
- MetricsEngine: `average_fps` and `average_frametime_ms` now both derive from the
  same rolling window mean, so `average_fps == 1000 / average_frametime_ms` always holds
- OverlayRuntime: `OnPresent` is now protected by a `std::mutex`; added
  `CopyLastSnapshot()` for safe cross-thread reads
- Dx11PresentHookWin: `Install()` now guards against a second concurrent install via
  `compare_exchange_strong`; `original_present_` is cleared on every error/remove path
  so the freed MinHook trampoline can never be called through a dangling pointer
  (**requires Windows build to verify — macOS/CI cannot exercise this path**)
- SessionLogger: added optional `max_samples` cap to prevent unbounded memory growth

### Added
- `vcpkg.json` manifest (SDL2, MinHook) for automatic dependency resolution
- `CopyLastSnapshot()` on `OverlayRuntime` for thread-safe snapshot reads
- Tests: `TestMetricsVarianceCorrectness`, `TestMetricsVarianceRollingEviction`,
  `TestMetricsNoNegativeVariance`, `TestExporterRoundTrip`

### Infrastructure
- `.gitignore`: unified build directory pattern (`/build*/`), added `vcpkg_installed/`,
  `CMakeUserPresets.json`, `compile_commands.json`
- README rewritten: platform table, Known Limitations section, accurate build instructions

### Added (M2)
- `debug_window_main.cpp` split from 2276-line monolith into focused modules under
  `src/app/debug_window/`: `types.h`, `bitmap_font`, `renderer`, `targeting`, `ui_panels`
  (all in `namespace dw`); main file reduced to ~450 lines of app/loop logic
- `nlohmann/json` v3.11.3 single-header vendored in `third_party/`; replaced hand-rolled
  JSON parser in `overlay_settings.cpp` with `input >> j` / `j.dump(2)` round-trip
- `MetricsEngine`: replaced O(n log n) copy+sort in `Snapshot()` with a `std::multiset<double>`
  mirror kept in sync during `PushSample()`; percentile queries now O(n·(1−p)) ≈ O(20)
  for 1% low and O(2) for 0.1% low at n=2000; min/max also O(1) from iterator endpoints
- Migrated tests from hand-rolled `Expect`/`ExpectNear`/`main()` to Catch2 v3 (`TEST_CASE`,
  `REQUIRE`, `CHECK`, `Approx`); each of 15 test cases now registers as an individual
  CTest entry; Catch2 obtained via `find_package` fallback to `FetchContent` — no CI changes
  required; `OverlaySettings` test split into 4 focused cases by subsystem
- CI: added `sanitize-asan` (ASan + UBSan) and `sanitize-tsan` (TSan) jobs on Ubuntu;
  `FRAMEWATCH_SANITIZE_ADDRESS` and `FRAMEWATCH_SANITIZE_THREAD` CMake options for local use;
  all 15 tests pass clean under ASan+UBSan locally (LSan active on Linux CI only)

### Added
- Frame-budget alerts in the debug window: a header chip shows the share of recent frames
  over the target budget (green/amber/red), and both the chip and the graph plot border
  pulse red when the latest frame is a real stutter (>1.5x the target budget)
- Configurable target FPS (`target_fps`, persisted) with frame-health coloring of the
  FPS/average/1%/0.1% stat cards and an emphasized target line on the frametime graph
  (cycle with `P` in the debug window)
- Frametime graph polish: filled area under the curve, thicker spike-colored line, and
  30/60/120 FPS frame-budget reference lines with a min/avg/max readout
- WndProc-routed input with optional capture/forward policy (`capture_input_when_panel_open`)
- Mouse-routed clickable controls in the F1 settings panel
- `--eject` mode in `framewatch_injector` for clean DLL unload via named Windows event
- Architecture (32/64-bit) validation in the injector before injection
- Hotkey hints toggle and compact mode persisted across sessions
- Target window selection and preview in the debug window settings drawer (keyboard navigation, pagination)
- Window size and position persistence in overlay settings
- SDL2 debug window (`framewatch_debug_window`) with live settings drawer
- External-process bootstrap path: injected DLL + injector
- Real DX11 Present detour path via MinHook (when available)
- In-game overlay renderer: alpha-blended panel, bitmap stats, frametime graph
- CSV and JSON session export
- Core frametime capture, rolling metrics, and session storage
- CI on Ubuntu, macOS, and Windows (GitHub Actions)

### Fixed
- HiDPI/Retina mouse hit-testing in the debug window: pointer events (clicks, hover,
  wheel) are now scaled from logical window points into renderer pixel space, so
  settings-panel controls line up with the cursor on high-DPI displays
- Bitmap font was missing the glyphs `J` and `Z`, which rendered as garbage for window
  titles/owners containing those letters

### Infrastructure
- MIT license
- GitHub issue templates and PR template
- SECURITY.md
- `.gitignore` additions: `.DS_Store`, `.claude/`
