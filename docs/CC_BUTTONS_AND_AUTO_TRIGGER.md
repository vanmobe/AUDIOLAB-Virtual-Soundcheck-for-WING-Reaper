# CC Buttons and Auto-Trigger

This guide documents the two live-operation features in the Behringer Wing plugin:

- WING CC button control of REAPER transport/markers/soundcheck
- Auto-triggered warning or recording based on input level

## 1. WING CC Button Control

Enable `Assign MIDI shortcuts to REAPER` in the plugin `Actions` section to activate plugin-managed CC handling and push command labels/bindings to the selected WING layer.
This is the default/primary workflow; manual REAPER shortcut mapping is only a fallback.

### Fixed CC Mapping (MIDI channel 1)

| CC # | Action |
|------|--------|
| 20 | Play |
| 21 | Record |
| 22 | Toggle Virtual Soundcheck |
| 23 | Stop (save recorded media) |
| 24 | Set Marker |
| 25 | Previous Marker |
| 26 | Next Marker |

Requirements on WING custom controls:

- Message type: `MIDI CC`
- MIDI channel: `1`
- Button press must send value `> 0`

Notes:

- The plugin handles CC input directly (not dependent on REAPER action-list shortcut reload timing).
- The selected `CC layer` is used for command/label sync and warning/record visuals.

## 2. Auto-Trigger

Auto-trigger is configured in the plugin `Auto Trigger` section and monitors REAPER signal level.

### Core settings

- `Enable trigger`: on/off
- `Mode`:
  - `WARNING`: sends warning behavior only (no transport record start)
  - `RECORD`: starts/stops REAPER recording automatically
- `Monitor track`: specific REAPER track, or `0`/auto behavior from plugin logic
- `Threshold`: trigger level in dBFS
- `Hold ms`: how long trigger stays active after level drops below threshold
- `CC layer`: WING layer used for warning/recording status visuals

### Trigger behavior

- Above threshold for attack window:
  - `WARNING` mode: warning OSC/visual state
  - `RECORD` mode: recording starts, recording state is shown on WING
- Below threshold past hold window:
  - warning/record state stops and WING status text/lights are cleared
- Manual transport interaction:
  - manual play/record suppresses warning behavior to avoid conflicting states
- Virtual soundcheck interaction:
  - when ALT source soundcheck mode is active on managed channels, auto-trigger monitoring is suppressed
  - this state is synced from WING so desk-side mode changes are respected (not only plugin button toggles)

## 3. SD Recording (Optional)

SD routing / recorder integration is optional and split into two pieces:

- `sd_lr_route_enabled`
  - requests Main LR routing to CARD 1/2 when the plugin connects to WING
- `sd_auto_record_with_reaper`
  - follows REAPER record start/stop and sends SD recorder start/stop via WING OSC fallback paths
  - applies the configured Main LR CARD routing before requesting SD start

Notes:

- The macOS dialog exposes the routing toggle plus a `MAIN` source pair selector.
- SD recorder control is best-effort OSC only; confirm routing and recorder state on WING before relying on SD capture.

## 4. Troubleshooting

- CC buttons do nothing:
  - confirm WING sends `MIDI CC` on channel `1`
  - confirm `Assign MIDI shortcuts to REAPER` is enabled
  - disable/re-enable this switch once to force button command re-sync
- Auto-trigger does not fire:
  - verify `Enable trigger` is ON
  - verify threshold is reachable
  - verify monitored track/input selection
- Trigger starts but does not stop:
  - increase/decrease `Hold ms` to expected decay behavior
  - verify input really drops below threshold
