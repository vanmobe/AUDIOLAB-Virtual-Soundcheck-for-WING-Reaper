# Setup Guide - Wing Connector

Complete setup instructions for building and installing the Wing Connector extension.

## Prerequisites

### Required Software

1. **CMake** (3.15 or later)
   - **macOS**: `brew install cmake`
   - **Windows**: Download from https://cmake.org/download/
   - **Linux**: `sudo apt install cmake` (Ubuntu/Debian) or `sudo yum install cmake` (RHEL/CentOS)

2. **C++ Compiler**
   - **macOS**: Xcode Command Line Tools
     ```bash
     xcode-select --install
     ```
   - **Windows**: Visual Studio 2019 or later
     - Download from: https://visualstudio.microsoft.com/downloads/
     - Install "Desktop development with C++" workload
   - **Linux**: GCC 8+ or Clang 7+
     ```bash
     sudo apt install build-essential  # Ubuntu/Debian
     sudo yum groupinstall "Development Tools"  # RHEL/CentOS
     ```

3. **Git** (for downloading dependencies)
   - **macOS**: Included with Xcode Command Line Tools
   - **Windows**: https://git-scm.com/download/win
   - **Linux**: `sudo apt install git` or `sudo yum install git`

## Step-by-Step Setup

### 1. Clone or Download Project

```bash
cd ~/Documents/code/CO_LAB
# If you haven't already, this is your project directory
```

### 2. Download Dependencies

#### Reaper SDK

1. Download the Reaper SDK:
   - Visit: https://www.reaper.fm/sdk/plugin/
   - Download `reaper_plugin.h` and `reaper_plugin_functions.h`

2. Create SDK directory and copy files:
   ```bash
   mkdir -p lib/reaper-sdk
   # Copy the downloaded files to lib/reaper-sdk/
   ```

#### oscpack Library

Download and set up oscpack:

```bash
cd lib
git clone https://github.com/RossBencina/oscpack.git
cd ..
```

Or download ZIP from: https://github.com/RossBencina/oscpack

### 3. Verify Directory Structure

Your project should look like this:

```
colab.reaper.recordwing/
├── CMakeLists.txt
├── build.sh
├── build.bat
├── config.json
├── src/
│   ├── main.cpp
│   ├── wing_osc.cpp
│   ├── track_manager.cpp
│   ├── wing_config.cpp
│   └── reaper_extension.cpp
├── include/
│   ├── wing_osc.h
│   ├── track_manager.h
│   ├── wing_config.h
│   └── reaper_extension.h
└── lib/
    ├── reaper-sdk/
    │   ├── reaper_plugin.h
    │   └── reaper_plugin_functions.h
    └── oscpack/
        ├── osc/
        └── ip/
```

### 4. Configure Your Wing Console

1. Power on your Behringer Wing console
2. Press **Setup** button
3. Navigate to **Network**
4. Enable **OSC** (toggle to ON)
5. Note the **IP Address** (e.g., `192.168.1.100`)
6. Verify **OSC Port** is `2223` (or note if different)

### 5. Update Configuration

Edit `config.json`:

```bash
nano config.json  # or use your preferred editor
```

Change the `wing_ip` to match your Wing's IP address:

```json
{
  "wing_ip": "192.168.1.100",  ← Update this
  "wing_port": 2223,
  ...
}
```

### 6. Build the Extension

#### macOS / Linux

```bash
chmod +x build.sh
./build.sh
```

#### Windows

```cmd
build.bat
```

### 7. Install to Reaper

After successful build, install the extension:

#### macOS

```bash
# Create Reaper plugins directory if it doesn't exist
mkdir -p ~/Library/Application\ Support/REAPER/UserPlugins

# Copy extension and config
cp install/reaper_wingconnector.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
cp config.json ~/Library/Application\ Support/REAPER/UserPlugins/
```

#### Windows

```cmd
REM Create directory if needed
mkdir "%APPDATA%\REAPER\UserPlugins"

REM Copy files
copy install\reaper_wingconnector.dll "%APPDATA%\REAPER\UserPlugins\"
copy config.json "%APPDATA%\REAPER\UserPlugins\"
```

#### Linux

```bash
# Create directory
mkdir -p ~/.config/REAPER/UserPlugins

# Copy files
cp install/reaper_wingconnector.so ~/.config/REAPER/UserPlugins/
cp config.json ~/.config/REAPER/UserPlugins/
```

### 8. Start Reaper

1. Launch or restart Reaper
2. Check the console for load message:
   - **View → Monitoring**
   - Look for: `Wing Connector 1.0.0 loaded`

3. Check Extensions menu:
   - **Extensions → Wing Connector** should appear

## Verification

### Test Network Connection

Before using the extension, verify network connectivity:

```bash
# Test ping to Wing console
ping 192.168.1.100  # Use your Wing's IP

# Should see replies like:
# 64 bytes from 192.168.1.100: icmp_seq=0 ttl=64 time=1.234 ms
```

### Test in Reaper

1. Open or create a new project
2. **Extensions → Wing Connector → Connect to Behringer Wing**
3. Wait for connection (3-5 seconds)
4. You should see:
   - Success dialog with track count
   - Tracks created in arrange view
   - Track names matching Wing channels

## Troubleshooting Setup

### CMake Errors

**"CMake not found"**
- Install CMake (see Prerequisites section)
- Ensure it's in your PATH
- Restart terminal after installation

**"Could not find reaper_plugin.h"**
- Download Reaper SDK files
- Place in `lib/reaper-sdk/` directory
- Verify file names are exact

**"oscpack not found"**
- Clone oscpack to `lib/oscpack/`
- Ensure directory structure is correct

### Build Errors

**"No matching constructor"** (C++ errors)
- Check compiler version (need C++17 support)
- Update compiler if necessary
- On macOS: update Xcode Command Line Tools

**Windows: "MSVC not found"**
- Install Visual Studio with C++ workload
- Run build from "Developer Command Prompt for VS"

**Linux: "undefined reference"**
- Install missing development packages
- `sudo apt install build-essential cmake`

### Installation Errors

**"Permission denied"** (macOS/Linux)
- Use `sudo` if needed for system directories
- Or use user plugins directory as shown above

**"File not found"** (Windows)
- Check `%APPDATA%` environment variable exists
- Manually navigate to: `C:\Users\YourName\AppData\Roaming\REAPER\UserPlugins`

### Runtime Errors

**Extension doesn't load in Reaper**
- Check Reaper version (need 6.0+)
- View console: **View → Monitoring**
- Look for error messages
- Verify file permissions (should be readable/executable)

**"Connection failed" in Reaper**
- Verify Wing is powered on
- Check network connection (ping test)
- Verify OSC is enabled on Wing
- Check firewall settings
- Try different ports in config.json

## Advanced Configuration

### Custom Build Options

Debug build:
```bash
./build.sh Debug
```

Specify CMake generator:
```bash
cd build
cmake .. -G "Unix Makefiles"  # or "Visual Studio 16 2019", etc.
```

### Network Configuration

If Wing is on a different subnet:
1. Ensure network routing is configured
2. May need to adjust firewall rules
3. Consider using dedicated network interface

### Multiple Wing Consoles

To support multiple Wings:
1. Create separate config files: `config_wing1.json`, `config_wing2.json`
2. Modify extension to load different configs
3. Or run multiple Reaper instances

## Next Steps

Once setup is complete:

1. **Read the User Guide**: See README.md for usage instructions
2. **Explore OSC Protocol**: Check docs/WING_OSC_PROTOCOL.md
3. **Customize Configuration**: Adjust colors, track prefixes, etc.
4. **Assign Keyboard Shortcuts**: For quick access to Wing actions
5. **Create Templates**: Save Reaper projects with Wing tracks pre-configured

## Getting Help

If you encounter issues:

1. Check this setup guide thoroughly
2. Review troubleshooting sections
3. Check Reaper console for error messages
4. Verify all prerequisites are installed
5. Test network connectivity to Wing
6. Check GitHub issues for similar problems

## Updating

To update the extension:

1. Pull latest code changes
2. Rebuild: `./build.sh`
3. Reinstall to Reaper plugins directory
4. Restart Reaper

## Uninstalling

To remove the extension:

```bash
# macOS
rm ~/Library/Application\ Support/REAPER/UserPlugins/reaper_wingconnector.dylib

# Windows
del "%APPDATA%\REAPER\UserPlugins\reaper_wingconnector.dll"

# Linux
rm ~/.config/REAPER/UserPlugins/reaper_wingconnector.so
```

Then restart Reaper.
