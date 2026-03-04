# Developer Setup Guide

This guide covers building COLAB.wing.reaper.virtualsoundcheck from source and installing it into REAPER for development.

## Prerequisites

- CMake `3.15+`
- C++17 compiler
  - macOS: Xcode Command Line Tools
  - Windows: Visual Studio (C++ workload)
  - Linux: GCC/Clang toolchain
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

macOS/Linux:

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

Linux:

```bash
mkdir -p ~/.config/REAPER/UserPlugins
cp install/reaper_wingconnector.so ~/.config/REAPER/UserPlugins/
cp config.json ~/.config/REAPER/UserPlugins/
```

## Verify in REAPER

1. Restart REAPER.
2. Confirm `Extensions -> COLAB.wing.reaper.virtualsoundcheck` is present.
3. Run connect flow and verify channels/tracks sync.

## Packaging and Release

- CI build matrix: `.github/workflows/ci.yml`
- Release packaging: `.github/workflows/release.yml`
- Packaging scripts:
  - `packaging/create_installer_macos.sh`
  - `packaging/create_installer_windows.ps1`
  - `packaging/create_installer_linux.sh`

Release tags matching `v*` trigger installer build + publish.

## Common Build Failures

- Missing REAPER SDK headers in `lib/reaper-sdk/`
- Missing `oscpack` checkout in `lib/oscpack/`
- Compiler toolchain not installed or not on PATH
- Platform-specific packaging tools missing (`pkgbuild`, Inno Setup, `dpkg-deb`)
