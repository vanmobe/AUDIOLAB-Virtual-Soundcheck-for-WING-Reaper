# MIDI Actions Reference

## Overview

The Wing Connector plugin now includes MIDI action mapping that allows buttons on the Behringer Wing to directly control REAPER transport and marker functions during soundcheck.

## How It Works

1. **Enable/Disable**: A checkbox in the Wing Connector dialog ("Enable MIDI Actions") allows you to turn this feature on/off
2. **Default State**: MIDI actions are **enabled by default**
3. **MIDI Protocol**: Listens for MIDI Control Change (CC) messages on **Channel 1**
4. **Button Press Detection**: Only responds to button press (value > 0), ignores release (value = 0)

## Wing Button Configuration

Configure your Wing custom buttons to send the following MIDI signals:

| Button Function | MIDI Signal | REAPER Action |
|----------------|-------------|---------------|
| **Set Mark** | CC#20 on Channel 1 | Insert marker at current position |
| **Prv Mark** | CC#21 on Channel 1 | Go to previous marker/project start |
| **Next Mark** | CC#22 on Channel 1 | Go to next marker/project end |
| **Record** | CC#23 on Channel 1 | Transport: Record |
| **Stop Record** | CC#24 on Channel 1 | Transport: Stop |
| **Play** | CC#25 on Channel 1 | Transport: Play |
| **Pause** | CC#26 on Channel 1 | Transport: Pause |

## Setup Instructions

### On the Wing Console

1. Access the Wing web interface
2. Navigate to the custom buttons configuration page
3. For each button, configure MIDI output as follows:
   - **Message Type**: Control Change
   - **MIDI Channel**: 1
   - **CC Number**: (see table above)
   - **Value on Press**: 127 (or any value > 0)
   - **Value on Release**: 0

### In REAPER

1. Ensure Wing is connected as a MIDI device to your computer
2. Open the Wing Connector dialog (Extensions → Wing Connector)
3. Verify "Enable MIDI Actions" checkbox is checked (default: ON)
4. Click "Setup Soundcheck" to configure your session
5. Test each button to confirm actions execute

## Troubleshooting

### MIDI Actions Not Working

1. **Check MIDI Connection**: Ensure Wing appears in REAPER's MIDI device list (Preferences → MIDI Devices)
2. **Verify Channel**: Confirm Wing buttons are sending on Channel 1
3. **Check CC Numbers**: Use REAPER's Actions → Show Action List → MIDI CC Learn to verify CC numbers
4. **Enable Feature**: Make sure "Enable MIDI Actions" checkbox is ON in Wing Connector dialog

### Conflicts with Other MIDI Devices

- The plugin filters for specific CC numbers (20-26) on Channel 1 only
- MIDI input is **not consumed** by the plugin - it passes through to other REAPER MIDI handlers
- If another device uses CC#20-26 on Channel 1, disable MIDI actions when not using Wing buttons

## Activity Log

When MIDI actions are enabled, you'll see log messages in the Wing Connector activity view:
```
MIDI Action: Set Marker (CC#20)
MIDI Action: Play (CC#25)
```

## Technical Details

- **Implementation**: REAPER `plugin_register("midi_input_hook")`
- **CC Range**: 20-26 (avoids common controller conflicts)
- **Channel**: 1 (configurable in future if needed)
- **Pass-Through**: MIDI messages are not consumed, allowing other plugins to also process them
- **Configuration Persistence**: Setting saved in `config.json`

## REAPER Command IDs (Reference)

| Action | Command ID |
|--------|-----------|
| Insert marker at current position | 40157 |
| Go to previous marker/project start | 40172 |
| Go to next marker/project end | 40173 |
| Transport: Record | 1013 |
| Transport: Stop | 1016 |
| Transport: Play | 1007 |
| Transport: Pause | 1008 |
