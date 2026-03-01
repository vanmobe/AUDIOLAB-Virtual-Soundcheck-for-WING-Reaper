# Quick Start Guide - Wing Connector C++ Extension

Get the Wing Connector extension up and running in 10 minutes!

## Step 1: Download Dependencies

Run the dependency setup script:

### macOS/Linux
```bash
./setup_dependencies.sh
```

This will:
- Set up directories for Reaper SDK
- Clone the oscpack library
- Guide you through manual SDK download

**Manual step**: Download Reaper SDK from https://www.reaper.fm/sdk/plugin/
- Files needed: `reaper_plugin.h` and `reaper_plugin_functions.h`
- Save to: `lib/reaper-sdk/`

## Step 2: Configure Your Wing

1. On the Behringer Wing console:
   - Press **Setup**
   - Go to **Network** → **OSC**
   - Enable OSC
   - Note the IP address (e.g., `192.168.1.100`)

## Step 3: Update Configuration

Edit `config.json`:
```json
{
  "wing_ip": "192.168.1.100",  ← Change this to your Wing's IP
  "wing_port": 2223,
   "listen_port": 2223,
  "channel_count": 48
}
```

## Step 4: Build the Extension

### macOS/Linux
```bash
./build.sh
```

### Windows
```cmd
build.bat
```

Wait for compilation to complete (1-2 minutes).

## Step 5: Install to Reaper

After successful build:

### macOS
```bash
cp install/reaper_wingconnector.dylib ~/Library/Application\ Support/REAPER/UserPlugins/
cp config.json ~/Library/Application\ Support/REAPER/UserPlugins/
```

### Windows
```cmd
copy install\reaper_wingconnector.dll %APPDATA%\REAPER\UserPlugins\
copy config.json %APPDATA%\REAPER\UserPlugins\
```

### Linux
```bash
cp install/reaper_wingconnector.so ~/.config/REAPER/UserPlugins/
cp config.json ~/.config/REAPER/UserPlugins/
```

## Step 6: Restart Reaper

1. Restart Reaper (or launch if not running)
2. Check console: **View → Monitoring**
3. Look for: `Wing Connector 1.0.0 loaded`

## Step 7: Connect!

1. **Extensions → Wing Connector → Connect to Behringer Wing**
2. Wait 3-5 seconds for connection
3. Success dialog shows number of tracks created
4. Tracks appear in arrange view with Wing channel names!

## What It Does

The extension will:
- ✅ Connect to your Wing console via OSC
- ✅ Query all channel names and settings
- ✅ Create matching tracks in Reaper
- ✅ Set up proper routing and naming
- ✅ Color-code tracks matching Wing colors
- ✅ Arm tracks for recording
- ✅ Loads automatically with Reaper
- ✅ Available in Extensions menu

## Available Actions

All available in **Extensions → Wing Connector**:
- **Connect to Behringer Wing** - Initial connection and track creation
- **Refresh Tracks** - Update tracks from current Wing state
- **Toggle Monitoring** - Enable real-time updates when Wing changes
- **Settings** - Configure extension options

## Assign Keyboard Shortcuts

For quick access:
1. **Actions → Show action list**
2. Find "Wing: Connect to Behringer Wing"
3. Click **Add** and assign a key (e.g., `Ctrl+Shift+W`)

## Troubleshooting

### Build Issues

**"CMake not found"**
- Install CMake: `brew install cmake` (macOS) or download from cmake.org
- Restart terminal after installation

**"reaper_plugin.h not found"**
- Download Reaper SDK files
- Place in `lib/reaper-sdk/` folder

**"oscpack not found"**
- Run `./setup_dependencies.sh`
- Or clone manually: `git clone https://github.com/RossBencina/oscpack.git lib/oscpack`

### Runtime Issues

**Extension doesn't load**
- Check Reaper version (need 6.0+)
- View console: **View → Monitoring**
- Verify file is in UserPlugins folder
- Check file permissions (should be executable)

**"Connection Failed"**
- Verify Wing IP address in config.json
- Ensure Wing and computer are on same network
- Check OSC is enabled on Wing
- Test with: `ping [wing_ip]`
- Check firewall allows UDP traffic

**"No Channel Data Received"**
- Increase `timeout` in config.json (try 10 seconds)
- Check Wing firmware is up to date
- Verify OSC port matches (default: 2223)

## Tips

### For Regular Use
- Extension loads automatically - no need to manually load
- Assign keyboard shortcuts for quick access
- Keep config.json in UserPlugins folder
- Update Wing IP if console address changes

### Advanced Features
- **Monitoring Mode**: Toggle to update tracks when Wing changes
- **Stereo Pairing**: Enable `create_stereo_pairs` in config.json
- **Custom Prefixes**: Change `track_prefix` to organize tracks
- **Color Coding**: Disable with `color_tracks: false` if not needed

## Next Steps

1. **Explore Real-Time Monitoring**
   - Toggle monitoring to sync changes from Wing to Reaper
   - Rename channel on Wing → automatically updates in Reaper

2. **Create Templates**
   - Set up tracks with Wing
   - Save as Reaper template
   - Quick start for sessions

3. **Customize Configuration**
   - Adjust colors in config.json
   - Set channel count to match your setup
   - Configure stereo pairing

4. **Learn the Protocol**
   - Read [docs/WING_OSC_PROTOCOL.md](docs/WING_OSC_PROTOCOL.md)
   - Understand Wing OSC commands
   - Based on Patrick Gillot's manual

## More Information

- **Full Documentation**: [README.md](README.md)
- **Setup Guide**: [SETUP.md](SETUP.md)
- **OSC Protocol**: [docs/WING_OSC_PROTOCOL.md](docs/WING_OSC_PROTOCOL.md)

## Need Help?

1. Check [SETUP.md](SETUP.md) for detailed troubleshooting
2. Review Reaper console for error messages
3. Verify all prerequisites are installed
4. Test network connection to Wing console

Ready to record! 🎵
