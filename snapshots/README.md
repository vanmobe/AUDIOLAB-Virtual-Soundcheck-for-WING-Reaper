# WING Button to REAPER MIDI Mapping

This guide explains how to map Behringer WING custom controls to REAPER actions used with Wing Connector.

## Scope

This repository currently documents manual setup.
If you create reusable mapping exports, you can store them in this folder.

## 1. Configure WING Custom Controls

On WING:

1. Open `Setup -> Remote -> Custom Controls`.
2. Choose a button slot.
3. Set message type to `MIDI` (`Note On` or `CC`).
4. Set destination to `USB`.
5. Use unique note/CC numbers per action.

## 2. Enable WING MIDI Input in REAPER

1. Open `Preferences -> Audio -> MIDI Devices`.
2. Enable WING MIDI input device.
3. Enable input for control messages.

## 3. Map Actions in REAPER

1. Open `Actions -> Show action list`.
2. Select target action.
3. Click `Add` in shortcuts.
4. Press WING button to learn MIDI message.
5. Save mapping.

## Suggested Mapping

- Button 1: Connect to WING
- Button 2: Refresh tracks
- Button 3: Toggle monitoring
- Button 4: Record
- Button 5: Play/Stop
- Button 6: Toggle soundcheck mode

## Troubleshooting

- No trigger in REAPER:
  - verify WING sends MIDI over USB
  - verify device enabled in REAPER
  - remap shortcut and retest
- Wrong action triggers:
  - ensure each button has unique MIDI note/CC
  - remove duplicate shortcuts in REAPER action list

## Related Docs

- [INSTALL.md](../INSTALL.md)
- [QUICKSTART.md](../QUICKSTART.md)
- [docs/USER_GUIDE.md](../docs/USER_GUIDE.md)
