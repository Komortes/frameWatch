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
4. propagate `Present` metadata through the hook/runtime/renderer path
5. persist benchmark sessions to CSV/JSON

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

## Debug Window

If `SDL2` is available on the machine, CMake also builds `framewatch_debug_window`.

Run it with:

```bash
./build/framewatch_debug_window
```

Controls:

- `Space`: pause or resume the synthetic benchmark
- `B`: start or stop benchmark recording
- `R`: reset the session
- `E`: export the current session
- `C`: cycle target dock anchor (`right-top`, `right-bottom`, `left-top`, `left-bottom`)
- `[` / `]`: decrease or increase overlay panel opacity
- `V`: toggle the frametime graph
- `I`: toggle the sidebar panels
- `Tab` / `Shift+Tab`: cycle detected desktop windows
- `G`: lock onto the current frontmost target window
- `F`: follow the selected target window
- `N`: clear the selected target
- `Esc`: quit

Exports are written to:

- `output/framewatch_debug_window.csv`
- `output/framewatch_debug_window.json`

For a non-GUI verification run:

```bash
./build/framewatch_debug_window --smoke-test
```

Targeting helpers:

```bash
./build/framewatch_debug_window --list-targets
./build/framewatch_debug_window --target-title "Safari"
./build/framewatch_debug_window --target-title "Safari" --follow-target
```

## Next implementation steps

- implement the real DX11 `Present` detour behind `PresentHook` and feed live `IDXGISwapChain*` into `PresentEvent`
- bind `Dx11OverlayRendererWin` to swap chain/device resources and replace the no-op render path with actual draw calls
- add runtime overlay settings and target-window aware positioning on Windows
- add DX12/Vulkan backends behind the same session/runtime interfaces
