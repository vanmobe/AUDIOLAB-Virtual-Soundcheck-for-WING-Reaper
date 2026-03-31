# WINGuard for REAPER

WINGuard is a C++ REAPER extension that connects to a Behringer WING console over OSC/UDP and automates track setup, channel sync, virtual soundcheck routing, and broader WING-driven workflows.

> Guard every take. Faster setup, safer record(w)ing!

- Status: Production-ready
- Platforms: macOS, Windows
- Installers: `.pkg` (macOS), `.exe` (Windows)
- License: MIT

> Disclaimer: This software is provided as-is for use at your own risk. No guarantees or official support are provided.

## Install

For end users, use the installers from GitHub Releases:

- https://github.com/vanmobe/colab.reaper.wing/releases

Platform-specific steps are in [INSTALL.md](INSTALL.md).

## System Requirements

- REAPER 6.0+
- Behringer WING (Compact, Rack, or Full)
- Same-network connectivity between REAPER host and WING
- OS support:
  - macOS 10.13+
  - Windows 10+

## Quick Start

1. Install the plugin for your platform.
2. Restart REAPER.
3. Open `Extensions -> WINGuard: Configure Virtual Soundcheck/Recording`.
4. Scan for a WING, select a discovered console or enter the WING IP manually, then connect/fetch channels.
5. Ensure the WING OSC port on the console is set to `2223` (the plugin uses `2223`).
6. Confirm tracks are created/updated in REAPER.

See [QUICKSTART.md](QUICKSTART.md) for the 5-minute flow.

## Key Features

- Automatic track creation from WING channel data
- Channel metadata sync (name, color, source-related info)
- Optional real-time monitoring for updates
- Virtual soundcheck setup for channels (USB/CARD routing + staged apply flow + validation status)
- Auto-trigger safety gate that stays suppressed while managed channels are in virtual soundcheck mode, including desk-side toggles on WING
- Automatic managed-channel source monitoring after apply, with retry tolerance for brief polling glitches and warning-only mono/stereo topology changes
- Record-source selection for channels, buses, and matrices
- Optional WING MIDI CC control (Play/Record/Stop/Markers/Virtual Soundcheck) with automatic button command assignment
- Separate selected-channel bridge action and planning path for SuperRack-style integration work
- Cross-platform dialog behavior:
  - macOS: native Cocoa dialogs
  - Windows: REAPER-native fallback dialogs

## User Documentation

- [docs/README.md](docs/README.md) - documentation map and reading order
- [INSTALL.md](INSTALL.md) - installer-first setup by platform
- [QUICKSTART.md](QUICKSTART.md) - shortest path to first connection
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) - day-to-day operation in REAPER
- [docs/WING_SELECTED_CHANNEL_BRIDGE.md](docs/WING_SELECTED_CHANNEL_BRIDGE.md) - bridge action split and implementation status
- [docs/CC_BUTTONS_AND_AUTO_TRIGGER.md](docs/CC_BUTTONS_AND_AUTO_TRIGGER.md) - CC mapping, auto-trigger, and SD recording notes

## Developer Documentation

- [SETUP.md](SETUP.md) - local dev/build environment
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - code structure and runtime flow
- [docs/WING_OSC_PROTOCOL.md](docs/WING_OSC_PROTOCOL.md) - implemented OSC subset + reference notes

## Build From Source

Prerequisites:

- CMake 3.15+
- C++17 compiler
- REAPER SDK headers in `lib/reaper-sdk/`
- `oscpack` sources in `lib/oscpack/`

Build:

```bash
./build.sh
```

Windows:

```bat
build.bat
```

Then copy the plugin binary + `config.json` into your REAPER `UserPlugins` folder.
Details are in [SETUP.md](SETUP.md).

## CI and Release

- CI build matrix: `.github/workflows/ci.yml`
- Tagged release packaging: `.github/workflows/release.yml`
- Tag pattern: `v*` publishes installers as release assets

## Support

When opening an issue, include:

- OS and version
- REAPER version
- WING model + firmware
- Relevant log output from REAPER
- Steps to reproduce
