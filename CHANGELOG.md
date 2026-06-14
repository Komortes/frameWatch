# Changelog

All notable changes to this project will be documented here.

## [Unreleased]

### Added
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
