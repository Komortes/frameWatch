# FrameWatch Mini

FrameWatch Mini is a lightweight local performance analysis tool for games.
This repository contains the initial MVP scaffold:

- frametime capture and session storage
- real-time metrics calculation
- CSV/JSON export
- overlay data model for FPS/frametime graph rendering
- Windows-only DX11 Present hook path

## Current scope

The core pipeline is already working end-to-end:

1. capture frame intervals
2. update rolling metrics
3. build overlay-ready graph data
4. propagate `Present` metadata through the hook/runtime/renderer path
5. persist benchmark sessions to CSV/JSON

On Windows, the repository now includes a real DX11 `Present` detour path when `MinHook` is available at build time.
The in-game renderer is no longer a pure stub: it now binds to the live swap chain, draws a lightweight geometry overlay with live stats and a frametime graph, and persists basic overlay settings between runs.

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

Windows DX11 detouring notes:

- `FRAMEWATCH_ENABLE_DX11_HOOK=ON` builds the DX11 hook path.
- If `MinHook` is installed, set `FRAMEWATCH_MINHOOK_ROOT=<path>` when needed.
- Without `MinHook`, the DX11 backend still builds, but `PresentHook::Install()` stays disabled with a descriptive runtime error.

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

On Windows, a second executable can validate the real DX11 hook/runtime path in-process:

```bash
./build/framewatch_dx11_hook_smoke
```

It creates a DX11 swap chain, installs the hook, issues live `Present` calls, and exports:

- `output/framewatch_dx11_hook_smoke.csv`
- `output/framewatch_dx11_hook_smoke.json`

The smoke app also exercises the minimal in-process DX11 overlay renderer:

- alpha-blended panel over the active back buffer
- bitmap text for the main live stats
- frametime polyline and frame-budget guide lines
- persistent overlay settings in `output/framewatch_dx11_overlay_settings.json`
- live hotkeys: `F2` benchmark toggle, `F3` export, `F4` reset session, `F6` overlay, `F7` graph, `F8` stats, `F9` dock, `F10` opacity down, `F11` opacity up
- runtime status feedback in the overlay footer for benchmark/export/reset actions

The repository also includes a minimal external-process bootstrap path on Windows:

- `framewatch_dx11_overlay.dll` boots `HookOverlayService` from `DllMain` on a worker thread
- `framewatch_injector` injects that DLL with `LoadLibraryW`
- bootstrap status is written to `output/framewatch_injected_status.txt` next to the DLL

Examples:

```bash
./build/framewatch_injector --list-windows
./build/framewatch_injector --window-title "Game Name"
./build/framewatch_injector --pid 1234
```

Optional injector flags:

```text
--dll <path>
--wait-ms <milliseconds>
```

Notes:

- the injector currently supports injection only; it does not expose a separate unload/eject CLI yet
- the injected DLL exports benchmark captures to `output/framewatch_injected_session.csv/json` next to the DLL
- anti-cheat protected games may block the injector or the overlay DLL outright

If needed, the DX11 overlay settings path can be overridden with:

```text
FRAMEWATCH_DX11_OVERLAY_SETTINGS=<path>
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
- `S`: open or close the in-window settings panel
- `T`: edit the target window query from inside the visualizer
- `C`: cycle target dock anchor (`right-top`, `right-bottom`, `left-top`, `left-bottom`)
- `[` / `]`: decrease or increase overlay panel opacity
- `V`: toggle the frametime graph
- `I`: toggle the sidebar panels
- `D`: reset overlay settings to defaults
- `Tab` / `Shift+Tab`: cycle detected desktop windows
- `Up` / `Down`: move target selection inside the settings-drawer list
- `Home` / `End`: jump to the first or last target in the list
- `PageUp` / `PageDown`: page the settings-drawer window list
- `G`: lock onto the current frontmost target window
- `F`: follow the selected target window
- `N`: clear the selected target
- `Esc`: quit

Exports are written to:

- `output/framewatch_debug_window.csv`
- `output/framewatch_debug_window.json`
- `output/framewatch_debug_window_settings.json`

The settings JSON keeps the current overlay layout and target-window restore state:
- graph/sidebar visibility
- panel opacity and dock anchor
- follow-target mode
- last selected target query
- window size and last position

The debug window can also show a live settings drawer with `S`. The drawer is actionable: you can click its buttons, type a target filter, use `Apply` / `Clear` next to the query field, hover a visible window row to preview its details, click it to lock onto it, navigate the list with `Up` / `Down` / `Home` / `End`, and page long window lists with `PageUp` / `PageDown` or the mouse wheel.

For a non-GUI verification run:

```bash
./build/framewatch_debug_window --smoke-test
```

Targeting helpers:

```bash
./build/framewatch_debug_window --list-targets
./build/framewatch_debug_window --target-title "Safari"
./build/framewatch_debug_window --target-title "Safari" --follow-target
./build/framewatch_debug_window --settings output/custom_overlay_settings.json
```

## Next implementation steps

- add a proper unload/eject path and process-architecture checks to the Windows injector
- add richer in-game controls and a fuller overlay settings surface for the Windows path
- replace the geometry-only text path with a more ergonomic renderer layer such as Dear ImGui
- add DX12/Vulkan backends behind the same session/runtime interfaces
