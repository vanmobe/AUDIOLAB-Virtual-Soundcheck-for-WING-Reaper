# WINGuard Installation Guide

WINGuard provides ready-to-use installers for the supported desktop platforms.

> Guard every take. Faster setup, safer record(w)ing!

- macOS: `.pkg`
- Windows: `.exe`

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

## macOS

1. Download the latest `WINGuard-MAC-v*.pkg`.
2. Double-click the package and follow prompts.
3. Restart REAPER.
4. Open the REAPER Actions list with `?`, search `WINGuard`, then run `WINGuard: Configure Virtual Soundcheck/Recording`.
5. Shortcut: `Cmd+Shift+W`.

Default plugin path:

- `~/Library/Application Support/REAPER/UserPlugins/`

## Windows

1. Download the latest `WINGuard-WIN-v*.exe`.
2. Run the installer and complete setup.
3. Restart REAPER.
4. Open the REAPER Actions list with `?`, search `WINGuard`, then run `WINGuard: Configure Virtual Soundcheck/Recording`.
5. Shortcut: `Ctrl+Shift+W`.

Windows note:

- The installed app should appear as `WINGuard` in Windows installed apps / Add or Remove Programs.

Default plugin path:

- `%APPDATA%\REAPER\UserPlugins\`

## Config Location Precedence

At runtime WINGuard resolves `config.json` in this order:

1. REAPER `UserPlugins/config.json`
2. `~/.wingconnector/config.json`

What this means in practice:

- Packaged installs normally use the `UserPlugins` copy because that is where the installer places the plugin and expected companion config.
- If the `UserPlugins` config is missing, WINGuard falls back to the home-directory config instead.
- If both copies exist, the `UserPlugins` copy wins.
- On Windows, `~/.wingconnector/config.json` maps to the current user's profile directory.

## First Run

1. Open the REAPER Actions list with `?`, search `WINGuard`, then run `WINGuard: Configure Virtual Soundcheck/Recording`.
2. Enter or discover the WING IP address.
3. Fetch channels and confirm track creation. WINGuard uses the fixed WING OSC runtime port `2223`.

Useful shortcuts:

- Main action: `Cmd+Shift+W` on macOS, `Ctrl+Shift+W` on Windows
- Existing-project adoption: `Cmd+Shift+I` on macOS, `Ctrl+Shift+I` on Windows

## Verify Installation

- `WINGuard` actions appear in the REAPER Actions list when you search for `WINGuard`.
- Plugin binary exists in your `UserPlugins` directory.
- Connection to WING succeeds without OSC timeout errors.

## Wing Button MIDI Control

WING CC button assignments are pushed by the plugin when `Assign MIDI shortcuts to REAPER` is enabled in the plugin window.

For mapping details (and legacy manual fallback), see:

- [docs/CC_BUTTONS_AND_AUTO_TRIGGER.md](docs/CC_BUTTONS_AND_AUTO_TRIGGER.md)
- [snapshots/README.md](snapshots/README.md)

## Where To Go Next

- [QUICKSTART.md](QUICKSTART.md) for the first connection flow
- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) for normal session operation
- [docs/README.md](docs/README.md) for the full documentation map

## Uninstall

Remove plugin and config from your REAPER `UserPlugins` path:

- macOS: `reaper_wingconnector.dylib`, `config.json`
- Windows: `reaper_wingconnector.dll`, `config.json`

If behavior still persists after removing the `UserPlugins` copy, also check for a fallback config in `~/.wingconnector/config.json`.

Then restart REAPER.
