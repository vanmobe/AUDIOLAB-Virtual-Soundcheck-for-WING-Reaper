# Releases

This folder is for local release artifacts when packaging manually.

Primary distribution is via GitHub Releases:

- https://github.com/vanmobe/colab.reaper.wing/releases

## Installer Types

- macOS: `COLAB-wing-reaper-virtualsoundcheck-<version>-macos-installer.pkg`
- Windows: `COLAB-wing-reaper-virtualsoundcheck-<version>-windows-setup.exe`
- Linux: `colab-wing-reaper-virtualsoundcheck_<version>_<arch>.deb`

## How Releases Are Built

Tagged pushes (`v*`) trigger `.github/workflows/release.yml`, which:

1. Builds plugin on macOS/Windows/Linux
2. Creates installer packages per platform
3. Publishes assets to the GitHub release for that tag

## Local Packaging (Maintainers)

- macOS: `packaging/create_installer_macos.sh`
- Windows: `packaging/create_installer_windows.ps1`
- Linux: `packaging/create_installer_linux.sh`

See [SETUP.md](../SETUP.md) for build prerequisites.
