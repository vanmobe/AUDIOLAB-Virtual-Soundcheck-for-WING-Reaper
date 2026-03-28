# Issue #14 – Selected-Channel Bridge Implementation Proposal

## Scope assumption

Issue #14 is treated as the implementation track for the selected-channel bridge flow that already exists as a dedicated REAPER action boundary.

This proposal keeps that scope isolated from virtual soundcheck and recorder workflows.

## Recommendation (best solution)

Implement a **polling-first selected-channel bridge pipeline** on top of the existing `WingOSC` connection and plugin lifecycle, with an internal abstraction that allows optional event/subscription support later.

Why this is the safest path right now:

- Live validation already confirmed `/$ctl/$stat/selidx` is readable.
- Subscription behavior for selection change is currently uncertain.
- The plugin is stability-sensitive and already has robust query/polling patterns.
- Polling can be bounded, debounced, and disabled by default to avoid regressions.

## Architecture proposal

### 1) Selection source adapter (core)

Add a small selection-source abstraction in `WingOSC` or adjacent core module:

- `ISelectionSource`
  - `Start()` / `Stop()`
  - callback: `on_selection_index_changed(int selidx_zero_based)`
- `PollingSelectionSource` (v1)
  - queries `/$ctl/$stat/selidx` at configurable interval
  - only emits on changed value

This keeps future subscription/event mode a drop-in replacement without rewriting mapping/MIDI/output code.

### 2) Normalized bridge model (extension)

Define a compact state model owned by extension layer:

- `ChannelSelection`
  - raw selected strip index (0-based)
  - resolved channel kind (`CH`, `AUX`, `BUS`, `MATRIX`, `DCA`, unknown)
  - resolved logical index (1-based within kind when relevant)
  - timestamp/sequence for dedupe

Map raw strip ids through one resolver function so behavior is deterministic and testable.

### 3) Mapping engine (bridge-specific)

Introduce bridge mapping config (JSON via current `wing_config` path):

- enable/disable bridge
- polling interval ms (default e.g. 100 ms)
- debounce ms (default e.g. 40 ms)
- output mode (start with Note On)
- map policy
  - v1 recommended: channel-range mapping table with default fallbacks

Do not change existing virtual soundcheck config keys; add a distinct bridge section.

### 4) MIDI output adapter (REAPER)

Add a thin output interface in extension layer:

- `IMidiBridgeOutput`
- v1 implementation uses REAPER MIDI output APIs
- emits only when resolved bridge target changes (not every poll)

Gate sends behind:

- active REAPER project state checks
- configured MIDI output availability
- diagnostics logging for skipped sends

### 5) Lifecycle integration

Wire the bridge pipeline behind the existing selected-channel bridge action:

- setup action toggles/configures bridge
- starts only after Wing connection is valid
- stops cleanly on disconnect/unload

No coupling into auto-record, SD recording, or virtual soundcheck flow.

## Suggested implementation phases

### Phase 0 – Safety scaffolding

- Add bridge config schema fields with defaults + load/save round-trip.
- Add no-op bridge manager + logging surface.

### Phase 1 – Polling selection source

- Implement periodic `/$ctl/$stat/selidx` query.
- Emit selection-changed events only on value changes.
- Add rate-limited logs for invalid/unexpected values.

### Phase 2 – Selection resolver + mapping

- Resolve strip index to normalized `ChannelSelection`.
- Apply mapping table and produce MIDI target payload.
- Add debounce and duplicate suppression.

### Phase 3 – MIDI sender + action wiring

- Send Note On for mapped target.
- Wire start/stop to bridge action and plugin lifecycle.
- Add clear UI status text in bridge setup flow.

### Phase 4 – Hardening

- Verify behavior with rapid selection changes.
- Verify no leakage to virtual soundcheck/recorder features.
- Add optional debug logging controls.

## Acceptance criteria for #14

1. Bridge can be enabled without affecting virtual soundcheck behavior.
2. With WING connected, changing selected strip yields debounced MIDI output changes.
3. No repeated MIDI flood while selection is unchanged.
4. Disconnect/reconnect cleanly restarts bridge without crashes.
5. Config persists bridge settings in `config.json` and survives restart.

## Risk controls

- **Risk:** Polling increases traffic.
  - **Control:** conservative default interval + change-only emission + debounce.
- **Risk:** Ambiguous strip-type mapping.
  - **Control:** central resolver + explicit unknown handling + diagnostics.
- **Risk:** REAPER extension instability from new runtime threads.
  - **Control:** reuse existing timing/query patterns and stop hooks in extension teardown.

## Out of scope for v1

- Bi-directional MIDI feedback.
- Dynamic runtime learning of mapping from third-party hosts.
- Replacing polling with subscription-only mode before protocol certainty.

## Why this is the best fit for this codebase

- Matches existing modular separation (`core` transport, `extension` lifecycle, `utilities` config).
- Keeps implementation incremental and reversible.
- Aligns with existing docs that isolate selected-channel bridge as a dedicated workflow.
- Minimizes regression risk in the plugin’s primary use cases.
