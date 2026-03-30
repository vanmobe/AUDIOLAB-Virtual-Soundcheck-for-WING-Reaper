# Issue #16 – Existing REAPER Project Adoption Proposal

## Scope Assumption

Issue #16 is treated as a design track for importing an already-built REAPER session into a WINGuard-managed recording and virtual soundcheck workflow.

This proposal keeps that work separate from:

- the main managed setup and rebuild flow
- the selected-channel bridge flow
- recorder coordination and auto-trigger behavior

## Recommendation

Implement project adoption as a separate REAPER action with a guided, confirmation-heavy workflow.

Recommended action name:

- `WINGuard: Adopt Existing REAPER Project for Soundcheck`

Why this is the safest fit:

- the main action now has a clear contract: connect, validate, rebuild, and switch an already managed setup
- imported REAPER sessions are not a trustworthy source of truth by default
- adoption needs a preservation decision that does not belong in the standard live setup flow
- a separate action keeps the riskier workflow opt-in and easier to explain

## V1 Product Shape

### Default mode

Default to `Create Managed Layer`.

Why:

- it preserves the imported project exactly as it arrived
- it avoids destructive rewrites when mapping guesses are incomplete
- it gives operators a safe first pass before they trust in-place adoption

`Adopt In Place` should still exist in v1, but as an explicit second option with stronger confirmation.

### Supported source families in v1

Support channel strips only in v1.

Do not support buses, matrices, mains, or FX-return style tracks in the first adoption workflow.

Why:

- channels are the primary virtual soundcheck use case
- buses and matrices are already record-only or workflow-sensitive in the current product
- limiting v1 to channels keeps mapping logic, stereo handling, and confirmation scope tractable

### Candidate-track rules in v1

Treat candidate tracks as advisory, not authoritative.

Recommended candidate rules:

1. Ignore tracks already managed by WINGuard (`P_EXT:WINGCONNECTOR_SOURCE_ID` present).
2. Ignore tracks with no media items and no obvious recording intent unless the user explicitly includes them.
3. Prefer tracks whose names loosely match WING channel names or stable channel labels.
4. Detect stereo candidates only when there is a strong adjacent-pair signal.
5. Leave unmatched tracks visible in the review, but unmanaged by default.

This means the scanner proposes likely adoption candidates, but the user must still confirm the mapping set.

## Proposed Workflow

### 1. Entry and prerequisites

User runs:

- `Extensions -> WINGuard: Adopt Existing REAPER Project for Soundcheck`

Workflow requirements:

- WING connection must be active first, or the adoption flow must connect before scanning
- channel metadata must be available from WING before mapping suggestions are shown

### 2. Scan and classify

The action scans the current REAPER project and classifies tracks into:

- already managed
- suggested channel candidates
- ambiguous
- ignored / unmanaged

The scan should not write anything.

### 3. Review mappings

Show a review step that includes:

- proposed REAPER track -> WING channel mapping
- stereo pair decisions
- unmatched WING channels
- unmatched REAPER tracks
- explicit unmanaged tracks that will be left alone

The operator can:

- accept or reject each suggested mapping
- change stereo to mono or mono to stereo where the UI allows it
- choose `Create Managed Layer` or `Adopt In Place`

### 4. Confirmation summary

Before any write:

- show how many tracks will become managed
- list the WING routing mode (`USB` / `CARD`)
- state whether imported tracks will be preserved in place or a new managed layer will be created
- state exactly which existing tracks will have input/output routing changed
- state that non-selected tracks will remain unmanaged

### 5. Apply

Only after confirmation:

- write `P_EXT:WINGCONNECTOR_SOURCE_ID` to adopted tracks
- update `last_selected_source_ids`
- apply WING routing and REAPER input/output routing for the confirmed mappings
- if `Create Managed Layer` was selected, create new managed tracks and leave imported tracks untouched

## Platform Strategy

### macOS

Use a dedicated adoption wizard in the native dialog layer.

This is the best place for:

- a multi-step review flow
- mapping summaries
- per-track confirmation UI

### Windows/Linux

Do not try to mirror a complex inline wizard in the first pass.

Recommended fallback:

- summary-first flow using REAPER dialogs
- optionally a compact mapping-spec text review if needed
- if the review UX becomes too brittle, keep adoption unavailable on fallback platforms until a safe minimal review flow exists

That is preferable to shipping a confusing, destructive fallback path.

## Write Rules

### Create Managed Layer

Allowed writes:

- create new managed tracks
- set managed track I/O and hardware output routing
- write managed source ids on the new tracks
- update `last_selected_source_ids`

Not allowed:

- rewriting the imported project tracks

### Adopt In Place

Allowed writes:

- write managed source ids to explicitly confirmed tracks
- update track input and hardware output routing for explicitly confirmed tracks
- update `last_selected_source_ids`

Not allowed in v1:

- silent renaming of tracks by default
- re-parenting folders
- deleting tracks
- reordering tracks automatically

Track names and colors should remain preserved by default in `Adopt In Place`.
If WING-based renaming is ever supported later, it should be a separate opt-in.

## Edge Cases

- Imported project already contains some WINGuard-managed tracks and some unmanaged tracks.
- Adjacent tracks look like a stereo pair in REAPER but the WING source is mono.
- REAPER track names partly match multiple WING channels.
- Imported project contains click, guide, FX return, stem print, or mix bus tracks.
- Current audio device does not expose enough `USB` or `CARD` I/O for the adopted mapping set.
- Operator cancels after review: no writes should have happened.
- Operator confirms `Create Managed Layer`: imported project should remain visually and structurally intact.

## Suggested Implementation Phases

### Phase 0 – Action boundary and scaffolding

- register a separate adoption action
- add an adoption dialog/wizard entry point
- add read-only scanning and classification helpers

### Phase 1 – Safe review-only prototype

- connect to WING
- scan REAPER project
- produce a review summary with suggested mappings
- no writes yet

### Phase 2 – Create Managed Layer

- apply confirmed mappings by creating new managed tracks only
- write managed ids on created tracks
- update routing and `last_selected_source_ids`

### Phase 3 – Adopt In Place

- allow explicit in-place adoption for confirmed tracks only
- preserve names, layout, folders, items, and FX
- rewrite only the agreed I/O and managed metadata

## Acceptance Direction

This issue should not be marked ready for build until v1 decisions are fixed on:

1. separate action name and boundary
2. `Create Managed Layer` as the default mode
3. channels-only scope for v1
4. advisory candidate mapping with explicit confirmation
5. exact fallback-platform strategy

## Recommendation Summary

Best v1:

- separate action
- channels only
- default to `Create Managed Layer`
- allow `Adopt In Place` only as an explicit advanced option
- no automatic renaming, reordering, or deletion
- no hidden integration into the main soundcheck dialog
