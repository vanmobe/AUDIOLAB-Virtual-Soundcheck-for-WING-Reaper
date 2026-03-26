# AUDIOLAB.wing.reaper.virtualsoundcheck (REAPER Extension for Behringer WING)

AUDIOLAB.wing.reaper.virtualsoundcheck is a C++ REAPER extension that connects to a Behringer WING console over OSC/UDP and automates track setup, channel sync, and virtual soundcheck routing.

- Status: Production-ready
- Platforms: macOS, Windows, Linux
- Installers: `.pkg` (macOS), `.exe` (Windows), `.deb` (Linux)
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
  - Debian/Ubuntu-based Linux with `.deb` package support

## Quick Start

1. Install AUDIOLAB.wing.reaper.virtualsoundcheck for your platform.
2. Restart REAPER.
3. Open `Extensions -> Behringer Wing: Configure Virtual Soundcheck/Recording`.
4. Enter your WING IP and port (default `2223`) and fetch channels.
5. Confirm tracks are created/updated in REAPER.

See [QUICKSTART.md](QUICKSTART.md) for the 5-minute flow.

## Key Features

- Automatic track creation from WING channel data
- Channel metadata sync (name, color, source-related info)
- Optional real-time monitoring for updates
- Virtual soundcheck setup for channels (USB/CARD routing + ALT source toggling)
- Record-source selection for channels, buses, and matrices
- Optional WING MIDI CC control (Play/Record/Stop/Markers/Virtual Soundcheck) with automatic button command assignment
- Cross-platform dialog behavior:
  - macOS: native Cocoa dialogs
  - Windows/Linux: REAPER-native fallback dialogs

## User Documentation

- [INSTALL.md](INSTALL.md) - installer-first setup by platform
- [QUICKSTART.md](QUICKSTART.md) - operator flow
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) - practical usage walkthrough
- [docs/CC_BUTTONS_AND_AUTO_TRIGGER.md](docs/CC_BUTTONS_AND_AUTO_TRIGGER.md) - CC mapping and auto-trigger behavior
- [snapshots/README.md](snapshots/README.md) - legacy/manual MIDI mapping fallback

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
