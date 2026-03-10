# FrameWatch Mini

FrameWatch Mini is a lightweight local performance analysis tool for games.
This repository contains the initial MVP scaffold:

- frametime capture and session storage
- real-time metrics calculation
- CSV/JSON export
- overlay data model for FPS/frametime graph rendering
- Windows-only DX11 hook extension point

## Current scope

The core pipeline is already working end-to-end:

1. capture frame intervals
2. update rolling metrics
3. build overlay-ready graph data
4. persist benchmark sessions to CSV/JSON

The actual DX11 `Present` detour and Dear ImGui renderer are intentionally left as platform integration points.
That keeps the repository buildable today while preserving the architecture needed for the real overlay/hook implementation.

## Project layout

```text
framewatch
├─ include/framewatch
│  ├─ core
│  ├─ exporter
│  ├─ hooks
│  └─ overlay
├─ src
│  ├─ app
│  ├─ core
│  ├─ exporter
│  ├─ hooks
│  └─ overlay
└─ tests
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Demo

The `framewatch_demo` executable simulates a benchmark run, prints live-style summary stats, and exports:

- `output/framewatch_session.csv`
- `output/framewatch_session.json`

Run it with:

```bash
./build/framewatch_demo
```

Optional flags:

```text
--frames <count>
--csv <path>
--json <path>
```

## Next implementation steps

- wire `IDXGISwapChain::Present` detouring through MinHook
- bootstrap Dear ImGui with a transparent DX11 overlay window
- add benchmark controls and runtime overlay settings
- add DX12/Vulkan backends behind the same interfaces
