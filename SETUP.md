# Developer Setup Guide

This guide covers building AUDIOLAB.wing.reaper.virtualsoundcheck from source and installing it into REAPER for development.

## Prerequisites

- CMake `3.15+`
- C++17 compiler
  - macOS: Xcode Command Line Tools
  - Windows: Visual Studio (C++ workload)
- Git
- REAPER SDK headers:
  - `lib/reaper-sdk/reaper_plugin.h`
  - `lib/reaper-sdk/reaper_plugin_functions.h`
- `oscpack` source in `lib/oscpack`

## Dependency Setup

```bash
./setup_dependencies.sh
```

Then verify:

- `lib/reaper-sdk/reaper_plugin.h`
- `lib/reaper-sdk/reaper_plugin_functions.h`
- `lib/oscpack/osc/OscOutboundPacketStream.h`

## Build

macOS:

```bash
./build.sh
```

Windows:

```bat
build.bat
```

## Install Built Plugin to REAPER

macOS:

```bash
mkdir -p ~/Library/Application\ Support/REAPER/UserPlugins
cp install/reaper_wingconnector.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
cp config.json ~/Library/Application\ Support/REAPER/UserPlugins/
```

Windows:

```bat
mkdir "%APPDATA%\REAPER\UserPlugins"
copy install\reaper_wingconnector.dll "%APPDATA%\REAPER\UserPlugins\"
copy config.json "%APPDATA%\REAPER\UserPlugins\"
```

Runtime config precedence:

1. `UserPlugins/config.json`
2. `~/.wingconnector/config.json`

Development implication:

- If you copy a development `config.json` into `UserPlugins`, that copy overrides any fallback config in `~/.wingconnector/`.
- If you want to test fallback behavior, remove or rename the `UserPlugins` copy first.

## Verify in REAPER

1. Restart REAPER.
2. Confirm `Extensions -> AUDIOLAB.wing.reaper.virtualsoundcheck` is present.
3. Run connect flow and verify channels/tracks sync.

## Packaging and Release

- CI build matrix: `.github/workflows/ci.yml`
- Release packaging: `.github/workflows/release.yml`
- Packaging scripts:
  - `packaging/create_installer_macos.sh`
  - `packaging/create_installer_windows.ps1`

Release tags matching `v*` trigger installer build + publish.

## Common Build Failures

- Missing REAPER SDK headers in `lib/reaper-sdk/`
- Missing `oscpack` checkout in `lib/oscpack/`
- Compiler toolchain not installed or not on PATH
- Platform-specific packaging tools missing (`pkgbuild`, Inno Setup)

## Config Troubleshooting

- If a packaged install and a local development build appear to use different settings, compare both `UserPlugins/config.json` and `~/.wingconnector/config.json`.
- If both files exist, WINGuard loads the `UserPlugins` copy.
- When neither file exists yet, new saves default to the `~/.wingconnector/config.json` path.

## Related Documentation

- [README.md](README.md) for the project overview
- [docs/README.md](docs/README.md) for the documentation map
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for code structure and runtime flow
- [docs/WING_OSC_PROTOCOL.md](docs/WING_OSC_PROTOCOL.md) for protocol details
