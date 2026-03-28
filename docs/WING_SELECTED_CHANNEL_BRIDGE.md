# Selected Channel Bridge

This document records the current implementation decision for bringing the selected-channel bridge into this plugin.

WINGuard tagline: Guard every take. Faster setup, safer record(w)ing!

## Product Direction

This repository is no longer only a virtual soundcheck helper. It is better treated as a broader WING integration plugin for REAPER.

That affects the bridge work in two ways:

- the selected-channel bridge must be exposed as its own REAPER action
- the bridge must not be folded into the live soundcheck or recorder-control flows

## Action Boundary

Current action split:

- `WINGuard: Configure Virtual Soundcheck/Recording`
  - main connection, source discovery, track setup, soundcheck, MIDI CC transport, and recorder-related workflows
- `WINGuard: Selected Channel Bridge Setup`
  - dedicated entry point for bridge-specific notes, validation status, and future bridge configuration

This keeps bridge work isolated from the existing recording and soundcheck behavior.

## What Is Still Missing

Live validation against a desk at `192.168.10.210` narrowed the protocol uncertainty:

- `/$ctl/$stat/selidx` is readable and reports the selected strip id
- reads return a 0-based value even though the protocol documentation describes writes as `1..76`
- a controlled `/*S~` subscription test did not emit `/$ctl/$stat/selidx` when the selected strip changed

That means the bridge no longer needs to wait on a generic “can this be read?” discovery step. The safer v1 decision is:

- use polling of `/$ctl/$stat/selidx`
- treat event subscription as a later optimization
- reuse the same connected WING session as the main WINGuard action instead of letting the bridge action guess its own target

The remaining runtime work is mapping the selected strip id into the intended logical bridge target and sending the corresponding MIDI output.

## Recommended Implementation Order

1. Capture the real selected-channel OSC event from a live WING session.
2. Add a dedicated polling selection-state module that converts `/$ctl/$stat/selidx` into a normalized `ChannelSelection` model.
3. Add bridge-specific config for polling interval, debounce, MIDI output target, and mapping policy.
4. Add a REAPER-backed MIDI output sender for Note On first.
5. Wire the selected-channel bridge pipeline behind the separate bridge action and bridge-specific enablement.

## Rename Direction

Near-term rename direction for user-facing text:

- favor `WINGuard` over `Virtual Soundcheck` for the main plugin entry point
- keep `Virtual Soundcheck` only where it describes that specific feature
- keep internal identifiers stable where changing them would break existing REAPER action bindings or packaging unexpectedly
