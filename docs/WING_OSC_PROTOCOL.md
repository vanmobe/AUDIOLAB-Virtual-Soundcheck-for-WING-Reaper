# Behringer Wing OSC Protocol Reference
## Based on Patrick Gillot's OSC Manual

This document describes the OSC protocol implementation for the Behringer Wing console, incorporating information from Patrick Gillot's comprehensive OSC manual.

## Overview

The Behringer Wing uses Open Sound Control (OSC) over UDP for remote control and monitoring. This provides programmatic access to virtually all console parameters.

## Connection Details

- **Protocol**: OSC 1.0
- **Transport**: UDP/IP
- **Default Port**: 2223 (configurable)
- **Encoding**: UTF-8
- **Byte Order**: Network byte order (big-endian)

## OSC Address Structure

Wing OSC addresses follow a hierarchical pattern:

```
/[node]/[parameter]/[subparameter]
```

### Main Nodes

- `/ch/` - Input channels (01-48)
- `/bus/` - Mix buses
- `/main/` - Main LR bus
- `/dca/` - DCA groups
- `/fx/` - Effects processors
- `/mtx/` - Matrix outputs
- `/config/` - Global configuration
- `/scene/` - Scene management

## Channel Commands (Input Channels 1-48)

### Format
Channel numbers are **zero-padded** two-digit format: `01`, `02`, ... `48`

### Configuration Parameters

#### Channel Name
```
/ch/[XX]/config/name
```
- **Type**: String
- **Description**: Channel name/label
- **Example**: `/ch/01/config/name "Kick Drum"`
- **Read/Write**: Both

#### Channel Color
```
/ch/[XX]/config/color
```
- **Type**: Integer (0-11)
- **Description**: Channel color ID
- **Values**:
  - 0: Off/Gray
  - 1: Red
  - 2: Green
  - 3: Blue
  - 4: Yellow
  - 5: Magenta
  - 6: Cyan
  - 7: Orange
  - 8: Purple
  - 9: Pink
  - 10: Brown
  - 11: White

#### Channel Icon
```
/ch/[XX]/config/icon
```
- **Type**: Integer (0-127)
- **Description**: Icon index from Wing's icon library
- **Common Icons**:
  - 0: None
  - 1: Microphone
  - 2: Instrument
  - 3: Keyboard
  - 4: Guitar
  - 5: Bass
  - 6: Drums
  - 7: Vocals
  - (See full icon list in Wing documentation)

#### Channel Source/Input
```
/ch/[XX]/config/source
```
- **Type**: Integer
- **Description**: Physical input routing
- **Values**: Depends on Wing model (Local, AES50-A, AES50-B, etc.)

#### Scribble Strip Style
```
/ch/[XX]/config/scribble/style
```
- **Type**: Integer
- **Description**: Scribble strip color/style
- **Values**: 0-7 (various color backgrounds)

### Mix Parameters

#### Fader Level
```
/ch/[XX]/mix/fader
```
- **Type**: Float (0.0 - 1.0)
- **Description**: Channel fader position
- **0.0**: -∞ dB
- **0.75**: 0 dB (unity)
- **1.0**: +10 dB

#### Mute
```
/ch/[XX]/mix/on
```
- **Type**: Integer (0 or 1)
- **Description**: Channel mute state
- **0**: Muted
- **1**: Unmuted

#### Pan
```
/ch/[XX]/mix/pan
```
- **Type**: Float (0.0 - 1.0)
- **Description**: Stereo pan position
- **0.0**: Hard left
- **0.5**: Center
- **1.0**: Hard right

### Channel Configuration (Extended)

#### Phantom Power (+48V)
```
/ch/[XX]/preamp/phantom
```
- **Type**: Integer (0 or 1)
- **Description**: Phantom power on/off

#### Gain
```
/ch/[XX]/preamp/gain
```
- **Type**: Float (0.0 - 1.0)
- **Description**: Preamp gain (0-60dB range)

#### High-Pass Filter
```
/ch/[XX]/preamp/hpf
```
- **Type**: Integer (0 or 1)
- **Description**: High-pass filter enable

#### Phase Invert
```
/ch/[XX]/preamp/invert
```
- **Type**: Integer (0 or 1)
- **Description**: Phase inversion

### Stereo Linking

#### Link
```
/ch/[XX]/config/link
```
- **Type**: Integer
- **Description**: Link channel to adjacent channel
- **Values**:
  - 0: Unlinked
  - 1: Linked (odd channels link to next even channel)

## Query Commands

To retrieve a value, send the OSC address without parameters:

```osc
SEND: /ch/01/config/name
RECEIVE: /ch/01/config/name "Vocal 1"
```

## Subscription for Real-Time Updates

Subscribe to parameter changes:

```osc
/subscribe [address]
```

Example:
```osc
/subscribe /ch/01/mix/fader
```

The console will then send updates whenever the subscribed parameter changes.

Unsubscribe:
```osc
/unsubscribe [address]
```

## Global Commands

### Console Information
```
/xinfo
```
Returns console model, firmware version, and capabilities.

### Show/Session Name
```
/config/name
```
Get or set the current show name.

### Scene Commands
```
/scene/load [index]
/scene/save [index]
/scene/name [index]
```

## Best Practices

### Query Rate Limiting
- Avoid querying faster than **100ms intervals**
- Wing may drop packets if overwhelmed
- Use subscriptions for continuous monitoring instead of polling

### Batch Queries
When querying multiple parameters, add small delays:
```cpp
for (int i = 1; i <= 48; i++) {
    query_channel(i);
    sleep_ms(10);  // 10ms delay between queries
}
```

### Error Handling
- UDP is unreliable - implement timeouts
- No explicit ACK - verify by querying value back
- Some parameters are read-only

### Connection Testing
Send `/xinfo` to verify console connectivity before bulk operations.

## Implementation Notes

### Byte Order
All multi-byte values use **network byte order** (big-endian).

### OSC Type Tags
Wing follows OSC 1.0 specification:
- `s`: String
- `i`: 32-bit integer
- `f`: 32-bit float

### Example OSC Packet (Hex)
Setting channel 1 name to "Vocal":
```
2f 63 68 2f 30 31 2f 63 6f 6e 66 69 67 2f 6e 61  |/ch/01/config/na|
6d 65 00 00 2c 73 00 00 56 6f 63 61 6c 00 00 00  |me..,s..Vocal...|
```

## Patrick Gillot's Contributions

Patrick Gillot's OSC manual provides:
- Complete parameter mapping for all Wing models
- Exact value ranges and scaling factors
- Detailed addressing schemes for effects and dynamics
- MIDI/OSC interaction documentation
- Layer and bank switching commands
- Routing matrix control

For the complete manual with all parameters, refer to:
**"Behringer WING OSC Remote Control Protocol"** by Patrick Gillot

## Additional Resources

- Behringer Wing Official Manual
- OSC 1.0 Specification: opensoundcontrol.org
- Patrick Gillot's GitHub: (if available)
- Wing User Forums: music-group.com

## Revision History

- v1.0 - Initial implementation based on Gillot manual
- Supports Wing Compact, Wing Rack, and Wing Full
