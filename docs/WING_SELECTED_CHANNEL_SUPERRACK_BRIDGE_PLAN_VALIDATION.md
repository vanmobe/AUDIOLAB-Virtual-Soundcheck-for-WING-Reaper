# WING Selected-Channel → SuperRack Bridge: Feasibility Validation & Adjusted Plan

Date: 2026-03-25

## Executive result

Your architecture is **feasible in this codebase**, but two assumptions need to be adjusted before implementation starts:

1. The plugin currently has **no selected-channel event pipeline** from WING. The current OSC implementation is focused on `/ch/*` metadata, routing, meters, and user-control paths—not selected-channel state. WP1 must therefore include protocol discovery and likely parser/routing expansion first.
2. The plugin currently handles **MIDI input hooks** (WING→REAPER commands), but does not yet have a dedicated MIDI output abstraction for sending mapping-triggered messages to an arbitrary output device. WP5 is therefore a foundational deliverable, not a late integration task.

With those adjustments, your proposed bridge architecture aligns well with the existing modular structure.

---

## What exists today (validated against current code)

### 1) Existing architecture is already modular and cross-platform

The project is already split into extension lifecycle, OSC core, configuration, track logic, and platform UI split (macOS native + cross-platform fallback), which is a good fit for adding a bridge pipeline without invasive refactors. See architecture overview and source layout notes.

### 2) OSC transport and parsing are substantial, but channel-selection is not implemented

`WingOSC` already supports:
- connection lifecycle,
- channel/routing queries,
- meter queries,
- user-control commands,
- callback-driven channel data updates.

However, `HandleOscMessage()` currently parses channel/routing/meter/user-control message families and does not emit a normalized “selected channel changed” event model.

### 3) Config system is JSON-based and extensible

`WingConfig` already loads/saves JSON and is the natural place to add bridge configuration (`midi output target`, `debounce`, `selection mapping`, schema versioning/migration).

### 4) Current MIDI feature set is inbound control-centric

The extension currently registers MIDI input hooks and maps inbound CC controls to REAPER actions, including transport/marker behavior and WING command sync. This validates that MIDI plumbing exists in project scope, but outbound selected-channel bridge behavior is not currently present.

---

## Feasibility assessment by your proposed modules

## A. WING OSC Transport Layer
**Feasible with extension work.** Reuse existing `WingOSC` networking and listener thread; add selected-channel signal acquisition (event or polling fallback) and classification updates.

## B. WING State Resolver
**Not present yet; should be added as new core module.** Current structures center on `ChannelInfo`; add a normalized selection model to avoid spreading WING-specific semantics.

## C. Mapping Engine
**Not present yet; straightforward addition.** JSON config support already exists and can host a mapping table.

## D. Debounce/State Change Engine
**Not present yet; recommended.** Existing code already uses timing/suppression patterns elsewhere, so implementation style is consistent.

## E. MIDI Output Engine
**Partially feasible, but missing implementation.** Inbound MIDI exists; outbound device enumeration/open/send API layer needs to be added.

## F. Configuration Layer
**Feasible now.** Existing `WingConfig` supports additive fields and save/load flow.

## G. Diagnostics/Logging
**Feasible now.** Logging callback + existing verbose OSC logging support the needed diagnostics.

---

## Adjusted implementation plan (recommended)

## Milestone 0 (new): Protocol confirmation spike (must happen first)

### Scope
- Identify real selected-channel source from WING under live capture.
- Confirm if path(s) are pushed vs. polled.
- Verify coverage for channel families needed in v1.

### Deliverables
- A short protocol note (raw path examples + payload examples + reliability notes).
- Decision: event-driven, polling, or hybrid fallback.

> Why this is moved up: current code does not yet encode selected-channel semantics; this is the biggest technical uncertainty.

## Milestone 1: Internal selection pipeline

### Scope
- Add `ChannelSelection` normalized model.
- Add resolver translating raw OSC into normalized selection.
- Add duplicate suppression + debounce.

### Deliverables
- Unit-testable selection normalization and state-transition behavior.
- Internal event emission (`SelectedChannelChanged`).

## Milestone 2: Config and mapping engine

### Scope
- Extend JSON schema (`version`, `wing selection source mode`, `midi output target`, mapping table, fallback behavior, debounce config).
- Build deterministic mapping engine from `ChannelSelection` → `OutputAction`.

### Deliverables
- Config validation + defaults + migration path.
- Mapping tests with edge cases (unknown family, missing mapping, ignored targets).

## Milestone 3: MIDI output abstraction + first transport

### Scope
- Add portable `IMidiOutput` interface and implementation compatible with REAPER extension runtime.
- Support Note On in v1; design for CC/Program extension.
- Add reconnect/error handling for missing ports.

### Deliverables
- `list/open/send/close` implementation.
- Manual “test send” diagnostic path.

## Milestone 4: End-to-end integration in plugin lifecycle

### Scope
- Wire OSC selection → resolver → debounce → mapping → MIDI output.
- Add settings surface (minimal UI or config-only path).
- Add debug logs for each stage.

### Deliverables
- First shippable feature: selected WING input channel emits mapped Note On to chosen MIDI output.

## Milestone 5: SuperRack validation and hardening

### Scope
- Validate routing with SuperRack Performer mapping.
- Capture latency/duplicate behavior under rapid selection changes.
- Run supported-platform checks (macOS/Windows build/runtime verification).

### Deliverables
- Integration notes + known limitations + recommended mapping pattern(s).

---

## Suggested v1 scope refinement

To de-risk delivery, make v1 explicitly:

- selection families: `input` only,
- actions: `note_on` only,
- behavior: duplicate suppression + debounce,
- configuration: JSON-only (UI optional),
- diagnostics: structured logs + manual MIDI send test.

Then expand to buses/DCA/main and CC/Program in v1.1.

---

## Key risks and mitigations

1. **Unknown selected-channel OSC source**
   *Mitigation:* Milestone 0 capture and document before implementation.

2. **MIDI output portability differences**
   *Mitigation:* strict `IMidiOutput` boundary and a REAPER-compatible backend first.

3. **Selection chatter/flooding**
   *Mitigation:* centralized debounce and last-sent state guard.

4. **Operator mismatch in SuperRack mapping expectations**
   *Mitigation:* keep bridge generic; publish tested mapping templates.

---

## Final recommendation

Proceed with implementation, but **re-sequence work** so protocol confirmation and MIDI output abstraction are treated as critical path items. Once those two are in place, the rest of your architecture fits the current plugin structure well and can be added incrementally with low coupling.
