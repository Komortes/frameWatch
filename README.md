# FrameWatch Mini

[![CI](https://github.com/Komortes/frameWatch/actions/workflows/ci.yml/badge.svg)](https://github.com/Komortes/frameWatch/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Lightweight game performance analysis tool. Captures frametimes, computes live metrics, renders an in-game overlay, and exports sessions to CSV/JSON.

## Features

- Frametime capture with rolling metrics: average, 1 % low, 0.1 % low, variance (Welford algorithm)
- Benchmark recording mode with separate metrics accumulation
- CSV and JSON session export
- In-game DX11 overlay (Windows): alpha-blended panel, frametime graph, frame-budget indicators
- External-process DLL injection path (Windows): `framewatch_injector` + `framewatch_dx11_overlay.dll`
- SDL2 debug window for development and cross-platform testing

## Platform support

| Feature | Windows | macOS | Linux |
|---------|---------|-------|-------|
| Core metrics and export | ✓ | ✓ | ✓ |
| SDL2 debug window | ✓ | ✓ | — |
| DX11 Present hook | ✓ | — | — |
| DX11 overlay renderer | ✓ | — | — |
| DLL injector | ✓ | — | — |

DX12 and Vulkan backends are planned.

## Project layout

```
framewatch
├─ include/framewatch
│  ├─ core          — FrametimeTracker, MetricsEngine, SessionLogger
│  ├─ exporter      — CSV and JSON exporters
│  ├─ hooks         — PresentHook interface, HookOverlayService
│  ├─ overlay       — OverlayRuntime, OverlaySettings, OverlayRenderer interface
│  ├─ platform      — WindowTargeting (macOS + stub)
│  └─ session       — PerformanceSession
├─ src              — Implementations mirroring include layout
│  └─ app           — Executable entry points
├─ tests            — Unit and integration tests
└─ third_party/imgui — Vendored ImGui (DX11 backend)
```

## Build

**Prerequisites:** CMake ≥ 3.21, a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

**Optional dependencies** (resolved automatically via vcpkg, or install manually):

| Dependency | Purpose |
|------------|---------|
| SDL2 | `framewatch_debug_window` |
| MinHook | Real DX11 Present detouring (Windows) |

With vcpkg:
```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

Without vcpkg, SDL2 is detected from the system. MinHook path can be provided manually:
```
-DFRAMEWATCH_MINHOOK_ROOT=<path>
```

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `FRAMEWATCH_ENABLE_DX11_HOOK` | `ON` | Build the DX11 hook path (Windows only) |
| `FRAMEWATCH_ENABLE_MINHOOK` | `ON` | Use MinHook for real Present detouring |
| `FRAMEWATCH_BUILD_WINDOWS_BOOTSTRAP` | `ON` | Build the injected DLL and injector (Windows only) |
| `FRAMEWATCH_BUILD_DEMO` | `ON` | Build the CLI demo |
| `FRAMEWATCH_BUILD_DEBUG_WINDOW` | `ON` | Build the SDL2 debug window if SDL2 is found |
| `FRAMEWATCH_BUILD_TESTS` | `ON` | Build unit tests |

## Executables

### `framewatch_demo`

Runs a synthetic benchmark, prints live stats, and exports:
- `output/framewatch_session.csv`
- `output/framewatch_session.json`

```bash
./build/framewatch_demo [--frames <count>] [--csv <path>] [--json <path>]
```

### `framewatch_debug_window` (requires SDL2)

SDL2-based visualizer with a live frametime graph, settings drawer, and window targeting.

```bash
./build/framewatch_debug_window
```

Key controls:

| Key | Action |
|-----|--------|
| `Space` | Pause / resume synthetic benchmark |
| `B` | Start / stop benchmark recording |
| `R` | Reset session |
| `E` | Export session |
| `S` | Open / close settings drawer |
| `P` | Cycle target FPS preset (30 / 60 / 90 / 120 / 144 / 165 / 240) |
| `V` | Toggle frametime graph |
| `C` | Cycle dock anchor |
| `[` / `]` | Decrease / increase panel opacity |
| `Tab` / `Shift+Tab` | Cycle detected desktop windows |
| `G` | Lock onto current frontmost window |
| `F` | Follow selected window |
| `Esc` | Quit |

Non-GUI smoke test:
```bash
./build/framewatch_debug_window --smoke-test
```

Window targeting helpers:
```bash
./build/framewatch_debug_window --list-targets
./build/framewatch_debug_window --target-title "Safari"
./build/framewatch_debug_window --target-title "Safari" --follow-target
./build/framewatch_debug_window --settings output/custom_settings.json
```

Settings are persisted to `output/framewatch_debug_window_settings.json`.

### `framewatch_dx11_hook_smoke` (Windows)

Validates the DX11 hook and overlay path in-process. Creates a swap chain, installs the hook, fires synthetic Present calls, and exports results.

```bash
./build/framewatch_dx11_hook_smoke
```

### `framewatch_injector` (Windows)

Injects `framewatch_dx11_overlay.dll` into a running process via `LoadLibraryW`.

```bash
# List injectable windows
./build/framewatch_injector --list-windows

# Inject by window title
./build/framewatch_injector --window-title "Game Name"

# Inject by PID
./build/framewatch_injector --pid 1234

# Eject
./build/framewatch_injector --eject --window-title "Game Name"
```

Optional flags: `--dll <path>`, `--wait-ms <milliseconds>`.

> **Note:** The injector and the target DLL must match the process bitness. Anti-cheat protected games may block injection entirely.

Injection status is written to `output/framewatch_injected_status.txt` next to the DLL.
The overlay settings path can be overridden with the environment variable `FRAMEWATCH_DX11_OVERLAY_SETTINGS=<path>`.

## Known limitations

- **DX11 only** — DX12 and Vulkan backends are planned.
- **No IPC** — An external process cannot read live metrics without DLL injection. A shared-memory / named-pipe path is planned.
- **Windows injection is blocked by anti-cheat** — This is expected behaviour.
- **Linux not supported** — A Vulkan layer or LD_PRELOAD hook path is planned.

## Security

See [SECURITY.md](SECURITY.md).

## License

MIT. See [LICENSE](LICENSE).
