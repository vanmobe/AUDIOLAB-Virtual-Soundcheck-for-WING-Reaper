# Behringer Wing OSC Command Reference

This document provides reference information about the OSC commands used to communicate with the Behringer Wing console. It incorporates knowledge from **Patrick Gillot's Behringer Wing OSC Manual**.

## Connection Details

- **Protocol**: OSC (Open Sound Control) over UDP
- **Default Port**: 2223
- **Transport**: UDP/IP
- **Encoding**: UTF-8
- **Implemented in**: `src/wing_osc.cpp`

## OSC Address Patterns

### Channel Configuration

#### Channel Name
```
/ch/{XX}/config/name
```
- `{XX}`: Channel number (01-48)
- Returns: String with channel name

#### Channel Color
```
/ch/{XX}/config/color
```
- `{XX}`: Channel number (01-48)
- Returns: Integer color ID (0-8)

#### Channel Source
```
/ch/{XX}/config/source
```
- `{XX}`: Channel number (01-48)
- Returns: String describing the input source

### System Commands

#### Get System Info
```
/xinfo
```
Returns general system information

#### Subscribe to Updates
```
/subscribe [address_pattern]
```
Subscribe to real-time updates for specific OSC addresses

## Channel Numbering

The Wing console uses zero-padded two-digit channel numbers:
- Channel 1: `01`
- Channel 10: `10`
- Channel 48: `48`

## Channel Types

- **Channels 1-48**: Main input channels
- **Aux/USB**: Additional channels (addresses may vary)
- **FX Returns**: Effects return channels

## Color IDs

Wing console uses numeric color IDs:
- 0: Gray/Default
- 1: Red
- 2: Green
- 3: Blue
- 4: Yellow
- 5: Magenta
- 6: Cyan
- 7: Orange
- 8: Purple

## Example OSC Messages

### Query Channel 1 Name
```
Send: /ch/01/config/name []
Receive: /ch/01/config/name ["Kick Drum"]
```

### Query All Channels
```python
for ch in range(1, 49):
    send(f"/ch/{ch:02d}/config/name", [])
    send(f"/ch/{ch:02d}/config/color", [])
```

## Network Setup on Wing

1. Press **Setup** button
2. Navigate to **Network**
3. Enable **OSC**
4. Set **OSC Port** to 2223 (or custom)
5. Note the Wing's IP address

## Troubleshooting

- **No response**: Check firewall settings, ensure OSC is enabled
- **Partial data**: Increase timeout or add delays between requests
- **Wrong data**: Verify channel numbers are zero-padded
- **Connection refused**: Check IP address and port number

## Additional Resources

- Behringer Wing Manual: Chapter on OSC/MIDI control
- OSC Protocol Specification: opensoundcontrol.org
- python-osc documentation: python-osc.readthedocs.io

## Notes

- The Wing may limit the rate of OSC queries
- Some parameters are read-only, others are read-write
- Real-time subscriptions reduce polling overhead
- UDP is unreliable; implement retries for critical data
