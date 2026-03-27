# Wing Connector Architecture

## Overview

This plugin is a modular C++ REAPER extension with clear separation between:

- REAPER extension lifecycle
- OSC communication with WING
- Track and routing logic
- Configuration/state management
- Platform-dependent UI integration

## Source Layout

- `src/extension/`
  - REAPER entrypoint and command registration
  - extension lifecycle and command handling
- `src/core/`
  - OSC transport, message build/parse, routing
- `src/track/`
  - track creation/update and related synchronization logic
- `src/utilities/`
  - config loading, logging, platform helpers, string utils
- `src/ui/`
  - dialog bridge and macOS native dialog implementations

Headers are split between:

- `include/wingconnector/` (public-facing module interfaces)
- `include/internal/` (internal implementation headers)

## Runtime Flow

1. REAPER loads plugin via `REAPER_PLUGIN_ENTRYPOINT`.
2. API function pointers are resolved.
3. Extension singleton initializes configuration and runtime components.
4. REAPER actions are registered for the main Wing Connector flow and the standalone selected-channel bridge setup flow.
5. User triggers an action; the matching UI or bridge entry point starts.
6. OSC queries fetch channel data from WING.
7. Track manager creates/updates REAPER tracks.
8. Optional auto-trigger and virtual soundcheck actions operate from dialog controls.
9. Optional MIDI CC transport/marker control is handled directly by plugin MIDI hooks/capture (with WING custom-button command syncing).

## UI Strategy

- macOS: native Objective-C++ dialogs in `src/ui/*_macos.mm`
- Windows/Linux: cross-platform bridge with REAPER-native dialogs/fallback flows (`dialog_bridge` + platform utilities)

This keeps feature parity while still using native UX on macOS.

## Build and Packaging

Build system: `CMakeLists.txt`

- Targets plugin as shared library:
  - `.dylib` on macOS
  - `.dll` on Windows
  - `.so` on Linux
- Links platform dependencies conditionally.
- Uses `oscpack` and REAPER SDK headers.

CI/CD:

- `.github/workflows/ci.yml`: build validation on macOS/Windows/Linux
- `.github/workflows/release.yml`: tagged release packaging + GitHub release assets

Packaging scripts:

- `packaging/create_installer_macos.sh`
- `packaging/create_installer_windows.ps1`
- `packaging/create_installer_linux.sh`

## Design Notes

- OSC parsing/routing is centralized in `src/core/`.
- Track operations are kept separate from transport/protocol concerns.
- Dialog and platform utilities isolate UI/platform complexity from core behavior.
- Configuration is file-based (`config.json`) for predictable deployment.
- WING shortcut command assignment is plugin-managed (not dependent on REAPER action-list shortcuts being reloaded at runtime).
- Selected-channel bridge work is intentionally isolated from live soundcheck and recorder flows until the WING event source is protocol-confirmed.
