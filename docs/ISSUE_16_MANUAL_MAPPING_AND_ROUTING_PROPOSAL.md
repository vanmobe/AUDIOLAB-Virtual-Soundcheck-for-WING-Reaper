# Issue #16 – Manual Mapping and Routing Proposal

## Scope

This proposal covers the next adoption step after basic in-place adoption:

- editable REAPER-track -> WING-channel assignment
- editable ordering
- global `USB` / `CARD` choice for the adoption run
- optional explicit playback-slot override
- mirrored REAPER input/output routing

This stays inside the separate action:

- `Extensions -> WINGuard: Adopt Existing REAPER Project for Soundcheck`

It does not move into the main managed setup/rebuild dialog.

## Goal

Let the operator review and edit the full adoption plan before anything is written:

- which REAPER track adopts which WING channel
- what order the confirmed mappings are applied in
- whether playback uses `USB` or `CARD`
- whether any track should override the auto-proposed playback slot

The result should still be a managed in-place adoption flow, but with operator-controlled mapping rather than mostly automatic matching.

## Product Decisions

These are treated as fixed for v1 of this step:

- Reordering applies at both levels:
  - channel level: which REAPER track lands on which WING channel
  - source/output level: what playback slot order is produced
- `USB` / `CARD` is one global choice for the adoption run, not per row.
- The current auto-proposed routing remains the initial suggestion.
- Explicit playback-slot override is optional.
- Mirrored routing stays enforced:
  - REAPER record input and hardware output always follow the same slot or pair
- Stereo slot rules stay enforced:
  - stereo rows may only use valid odd-start slot pairs
- Duplicate playback slots must never be applied.
- Windows/Linux fallback should follow the same style as the current soundcheck action fallback.

## Recommended UX

### macOS

Use a dedicated table-based adoption editor.

One row represents one adoptable REAPER track.

Recommended columns:

- `Track`
  - REAPER track index and current name
- `Shape`
  - mono / stereo
- `Suggested`
  - suggested WING channel from scanner
- `Channel`
  - final WING channel dropdown
- `Mode`
  - global `USB` / `CARD` selector above the table, not repeated per row
- `Slot`
  - playback slot dropdown, using the current global mode
- `I/O Preview`
  - read-only mirrored REAPER input/output preview
- `Status`
  - ok / warning / conflict

Recommended interaction rules:

- Show all channel and slot options in dropdowns.
- Gray out options that are not currently selectable when that is easy and unambiguous.
- Also support temporary conflict states:
  - if the user creates a conflict, show it clearly in the row status and overall summary
  - block apply until all conflicts are resolved

This hybrid approach is preferable to only hiding options, because it keeps the full routing space visible while still enforcing correctness at apply time.

### Windows/Linux fallback

Use the same pattern as the current cross-platform soundcheck action:

1. show a generated review/help summary
2. collect a compact structured input string with `GetUserInputs()`
3. validate it
4. show a confirmation summary
5. apply only if valid

Fallback should not try to mirror the full macOS table UI.

Recommended fallback input fields:

1. mapping spec
2. mode (`USB` / `CARD`)
3. optional slot override spec
4. apply now (`1/0`)

Recommended fallback format:

```text
Track2=CH8;Track3=CH2;Track4=CH4
```

and optional slot override spec:

```text
CH8=USB9-10;CH2=USB1;CH4=USB2
```

Rules:

- mapping spec is required
- slot override spec is optional
- if slot override spec is empty, auto-allocation is used from confirmed order
- duplicate channels or duplicate slots fail validation
- stereo rows must validate as odd-start pairs

## Data Model

The current `SourceSelectionInfo` model is not sufficient for this step.

Introduce a dedicated adoption-plan row structure.

Recommended fields:

```cpp
struct AdoptionPlanRow {
    int track_index;                  // REAPER track index
    std::string track_name;           // current REAPER name
    bool stereo;                      // derived from imported media / adopted shape

    int suggested_channel;            // scanner output
    int selected_channel;             // operator choice

    bool slot_overridden;             // operator changed slot explicitly
    int slot_start;                   // 1-based USB/CARD slot start
    int slot_end;                     // same as start for mono, start+1 for stereo

    bool conflict;                    // derived validation state
    std::string conflict_reason;      // user-visible problem description
};
```

Use a second container for plan-level state:

```cpp
struct AdoptionPlan {
    std::vector<AdoptionPlanRow> rows;
    std::string output_mode;          // "USB" or "CARD"
};
```

Why this is needed:

- channel assignment is no longer identical to playback-slot allocation
- current sequential allocation hides operator intent inside row order
- conflict validation needs a first-class place to live before apply

## Routing Rules

### Auto-proposed routing

Default behavior should still match the current engine:

- rows are ordered in confirmed plan order
- if no slot override exists, allocate sequentially
- stereo rows take the next valid odd-start pair
- mono rows fill gaps where possible

This keeps the current `CalculateUSBAllocation()` behavior as the baseline proposal.

### Explicit slot override

When a row has an explicit slot override:

- use the requested slot instead of sequential allocation
- validate against the current global mode bank size
  - `USB`: 48
  - `CARD`: 32
- validate uniqueness across the plan
- validate stereo constraints:
  - stereo rows must specify a valid odd-start pair
  - mono rows must specify a single slot

### Mirrored REAPER I/O

Mirrored routing remains mandatory in v1:

- REAPER record input follows the resolved slot or pair
- REAPER hardware output follows the same resolved slot or pair

The UI should not allow the operator to split input/output routing.

## Conflict Rules

Apply must be blocked until all conflicts are resolved.

Conflicts include:

- duplicate selected WING channel
- duplicate playback slot or overlapping pair
- stereo row assigned to an even-start or incomplete pair
- slot outside active mode bank
- already managed track/channel collision that is not explicitly part of the current re-adopt plan

Recommended row status wording:

- `OK`
- `Duplicate Channel`
- `Duplicate Slot`
- `Invalid Stereo Pair`
- `Out Of Range`
- `Managed Conflict`

Recommended summary wording:

- `3 conflicts must be fixed before Apply`

## Architecture Impact

### UI layer

Current summary-first adoption in [src/ui/dialog_bridge.cpp](../src/ui/dialog_bridge.cpp) is not sufficient.

Needed:

- scan phase
- editable plan model
- validation pass
- final conversion from `AdoptionPlan` to setup/apply payload

### Extension layer

Current [`SetupSoundcheckFromSelection()`](../src/extension/reaper_extension.cpp) assumes:

- selected channels are already final
- playback-slot allocation comes from order only

Needed:

- either a new setup entrypoint that accepts explicit allocations
- or a refactor so the allocator can consume explicit slot overrides cleanly

Recommended direction:

- keep `SetupSoundcheckFromSelection()` for existing flows
- add a dedicated adoption apply path that accepts:
  - selected/adopted channels
  - global mode
  - explicit slot plan where present

### Core routing layer

Current [`CalculateUSBAllocation()`](../src/core/wing_osc.cpp) should remain usable as:

- the default auto-allocation generator

But it should no longer be the only routing path for adoption.

Recommended addition:

- a validator/normalizer that merges:
  - explicit slot overrides
  - auto-allocation for remaining rows

and outputs a final `USBAllocation` list for apply.

## Apply Flow

Recommended apply sequence:

1. Build `AdoptionPlan` from scan results and operator edits.
2. Validate rows and plan-level conflicts.
3. Resolve final playback allocation:
   - explicit slot overrides first
   - auto-fill remaining rows by confirmed order
4. Show final confirmation summary.
5. Mark adopted tracks managed in place.
6. Apply WING routing and mirrored REAPER routing.
7. Update `last_selected_source_ids`.

## Suggested Confirmation Summary

Before final apply, show:

- number of adopted tracks
- global mode (`USB` / `CARD`)
- exact channel assignments
- exact playback slot assignments
- which rows are stereo
- confirmation that REAPER input/output will be mirrored
- confirmation that imported tracks stay in place

Example:

```text
Adopt 8 tracks in place using USB mode.

CH1 <- Track 2 (KICK) -> USB1
CH2 <- Track 3 (SNARE) -> USB2
CH8 <- Track 9 (OH) -> USB9-10

REAPER input and hardware output will be mirrored to the same slots.
Apply now?
```

## Edge Cases

- Imported project already contains some adopted tracks and some unmanaged tracks.
- Operator remaps only a subset of tracks.
- Stereo imported track is moved to a different WING channel but keeps auto slot routing.
- Stereo imported track is given an explicit invalid slot override.
- `CARD` mode selected with more than 32 required playback channels.
- Rows are conflict-free individually, but overlap after mixed stereo/mono slot resolution.

## Recommendation

This is the recommended shape for the next `#16` build step.

With the decisions now fixed, the remaining work is implementation design, not product discovery.

## Readiness

Issue
- Define the editable adoption workflow for manual channel mapping, ordering, and routing.

Problem
- Current adoption is too automatic for real imported-project workflows.

Proposed Direction
- Add a table-based editable adoption plan on macOS and a structured fallback input flow on Windows/Linux.
- Keep `USB` / `CARD` global.
- Keep mirrored I/O mandatory.
- Make explicit slot override optional.
- Block apply until all conflicts are resolved.

Alternatives
- Keep sequential allocation only: rejected
- Push this into the main setup dialog: rejected
- Hide conflicting options entirely with no conflict model: rejected

Checks
- Architecture: requires a first-class adoption-plan model
- UX: clear and operator-controlled
- UI/UX: stable direction for both macOS and fallback
- Compatibility: isolated to adoption action
- API/Data: needs new plan and allocation structures
- Risk: manageable if apply is hard-blocked on conflicts
- Testing: clear manual matrix

Readiness
- Status: ready
- Reason: User intent, workflow shape, validation rules, and platform direction are now clear enough for build
