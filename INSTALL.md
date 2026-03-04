# COLAB.wing.reaper.virtualsoundcheck Installation Guide

COLAB.wing.reaper.virtualsoundcheck provides ready-to-use installers for all supported desktop platforms.

- macOS: `.pkg`
- Windows: `.exe`
- Linux: `.deb`

Download installers from:

- https://github.com/vanmobe/colab.reaper.wing/releases

## System Requirements

Runtime requirements for all platforms:

- REAPER 6.0 or newer
- Behringer WING console (Compact, Rack, or Full)
- Network connectivity between computer and WING

Platform requirements:

- macOS: macOS 10.13+
- Windows: Windows 10 or newer
- Linux: Debian/Ubuntu-compatible system with `.deb` support

## macOS

1. Download the latest `COLAB-wing-reaper-virtualsoundcheck-*-macos-installer.pkg`.
2. Double-click the package and follow prompts.
3. Restart REAPER.
4. Open `Extensions -> COLAB.wing.reaper.virtualsoundcheck`.

Default plugin path:

- `~/Library/Application Support/REAPER/UserPlugins/`

## Windows

1. Download the latest `COLAB-wing-reaper-virtualsoundcheck-*-windows-setup.exe`.
2. Run the installer and complete setup.
3. Restart REAPER.
4. Open `Extensions -> COLAB.wing.reaper.virtualsoundcheck`.

Default plugin path:

- `%APPDATA%\REAPER\UserPlugins\`

## Linux (Debian/Ubuntu)

1. Download the latest `colab-wing-reaper-virtualsoundcheck_*_amd64.deb` (or matching arch).
2. Install with your package manager, for example:

```bash
sudo apt install ./colab-wing-reaper-virtualsoundcheck_<version>_<arch>.deb
```

3. Restart REAPER.
4. Open `Extensions -> COLAB.wing.reaper.virtualsoundcheck`.

Default plugin path:

- `~/.config/REAPER/UserPlugins/`

## First Run

1. Go to `Extensions -> COLAB.wing.reaper.virtualsoundcheck -> Connect to Behringer Wing`.
2. Set WING IP and port (default `2223`).
3. Fetch channels and confirm track creation.

## Verify Installation

- Extension appears under the `Extensions` menu in REAPER.
- Plugin binary exists in your `UserPlugins` directory.
- Connection to WING succeeds without OSC timeout errors.

## Optional: Wing Button MIDI Control

For optional custom button mapping from WING to REAPER actions, see:

- [snapshots/README.md](snapshots/README.md)

## Uninstall

Remove plugin and config from your REAPER `UserPlugins` path:

- macOS: `reaper_wingconnector.dylib`, `config.json`
- Windows: `reaper_wingconnector.dll`, `config.json`
- Linux: `reaper_wingconnector.so`, `config.json`

Then restart REAPER.
