# Issue #13 – Existing Project Soundcheck Intake

## Restated Problem

Issue #13 is asking about two operator situations that now need to be treated separately:

1. You already used WINGuard to set up recording and soundcheck, then come back later and want confidence that the project and console are ready again.
2. You receive or open an existing REAPER project that was not created by WINGuard and want to configure the WING from that project.

In the current codebase, those are not the same problem.

## What The Current Product Already Does

The current implementation already covers much of the first case:

- managed tracks persist source identity through `P_EXT:WINGCONNECTOR_SOURCE_ID`
- `last_selected_source_ids` persists the last applied source set in `config.json`
- connect-time flow starts managed-source monitoring
- the macOS dialog re-runs live setup validation against WING and REAPER
- output mode changes can stage a rebuild using the current applied selection
- soundcheck toggle is blocked until validation passes
- managed mono/stereo-compatible source changes can auto-refresh routing
- managed topology breaks warn instead of silently rewriting the setup

This means the "day after" scenario is no longer a missing routing engine. It is mainly a discoverability and workflow clarity problem.

The second case is still genuinely unsolved:

- the product has no authoritative way to infer WING channel ownership from arbitrary REAPER tracks
- imported sessions may have unrelated names, foldering, buses, edits, FX returns, or mixed mono/stereo layouts
- replacing or rewriting those tracks using the current setup flow would be destructive or confusing

## Recommended Product Shape

Do not keep Issue #13 as one build item. Split it into two tracks.

### Track A: Managed Project Resume / Rebuild

This should be the supported "come back tomorrow and continue" workflow.

Recommended end-user workflow:

1. Open the existing REAPER project.
2. Open `Extensions -> WINGuard: Configure Virtual Soundcheck/Recording`.
3. Connect to the WING.
4. Let WINGuard validate the existing managed setup.
5. If validation is ready, the user can switch between `Live Mode` and `Soundcheck Mode` immediately.
6. If the user changed `USB`/`CARD` mode or needs to rebuild routing, the UI should offer an explicit `Rebuild Current Setup` action that reuses the currently managed source selection.
7. If topology changed or the setup is partial, the UI should tell the user exactly what must be re-applied.

What to do:

- Make the resume path explicit in UI copy and docs.
- Add an explicit rebuild action or clearly named affordance for "reuse current managed selection".
- Keep WING as the source of truth for channel/source discovery.
- Keep validation as the gate before soundcheck toggle, auto-trigger, and MIDI-dependent actions.

Why this fits the codebase:

- it reuses the current staged apply model
- it preserves the extension/core/ui boundaries
- it aligns with managed-source monitoring and current validation logic
- it avoids speculative mapping logic

### Track B: Adopt Existing REAPER Project

This should not be hidden inside the normal soundcheck flow.

Recommended end-user workflow:

1. Open an existing REAPER project that was not created by WINGuard.
2. Open a separate action such as `Adopt Existing Project for WING Soundcheck`.
3. Connect to the WING first so channel metadata is available.
4. WINGuard scans REAPER tracks and proposes candidate mappings to WING channels.
5. The user reviews and confirms each mapping, especially for stereo pairs, buses, matrices, and unmatched tracks.
6. The user chooses whether to:
   - adopt tracks in place, preserving layout/FX/items
   - or create a new managed recording layer
7. Only after confirmation does WINGuard write managed source ids and apply console routing.

What to do:

- Treat this as a separate design issue, not part of the normal reconnect flow.
- Require explicit user confirmation before any routing or track metadata rewrite.
- Prefer assisted/manual mapping with suggestions, not fully automatic inference.

Why this needs its own issue:

- it introduces a second source of truth
- it needs a preservation strategy for existing project structure
- it has much higher UX and data-safety risk than the managed-project case

## Edge Cases That Matter

- A project contains WINGuard-managed tracks plus unrelated tracks.
- A friend project uses names that do not match current WING channel names.
- Stereo content is split across mono tracks, or the desk source changed mono/stereo topology since recording.
- The project includes buses, matrices, FX returns, click tracks, or print stems that should not become managed inputs.
- The current REAPER audio device does not expose enough `USB` or `CARD` I/O for the requested mode.
- The user changes output mode without wanting track replacement.
- The console channel source changed but remained mono/stereo-compatible; current managed monitoring can refresh this automatically.
- The console source became unreadable or changed topology; current behavior correctly warns and waits for manual review.
- Windows/Linux fallback UI cannot absorb a complex adoption workflow cleanly without a separate dialog design.

## Recommendation

Suggested decision:

- Keep Issue #13 open only as an umbrella or rewrite it to the managed resume/rebuild case.
- Create a new build issue for `Make managed project resume/rebuild explicit in the UI and docs`.
- Create a separate design issue for `Adopt an existing REAPER project into a managed WING soundcheck workflow`.

Suggested priority:

- Do Track A first.
- Do not implement blind REAPER-to-WING auto-mapping in the main dialog.

## Readiness Packet

Issue
- Clarify and complete the workflow for resuming or rebuilding soundcheck from an existing REAPER project.

Problem
- The current product already has managed resume/validation mechanics, but the workflow is not explicit enough for users returning to an existing project. The separate imported-project case still lacks a safe design.

Proposed Direction
- Split the work. Make managed-project resume/rebuild explicit and buildable now; handle imported-project adoption as a separate workflow and issue.

Alternatives
- Keep one issue and implement automatic REAPER-to-WING inference in the main setup flow.
- Rejected because it mixes a small UX/discoverability improvement with a high-risk mapping feature.

Checks
- Architecture: Managed resume fits existing extension/core/ui boundaries; imported-project adoption would add a new workflow surface and mapping layer.
- UX: Users need a clear "ready to switch" signal and a named rebuild action for managed projects.
- UI/UX: Resume belongs in the current Reaper tab; external-project adoption needs its own guided flow.
- Compatibility: Managed resume is backward-compatible with existing `config.json`, managed track ids, and staged apply behavior.
- API/Data: Managed resume needs no schema change; external adoption likely needs additional persisted mapping state or adoption markers.
- Risk: The main risk is destructive or incorrect mapping when treating arbitrary REAPER tracks as authoritative.
- Testing: Managed resume can be validated with current build + manual reconnect scenarios; external adoption would need a broader manual matrix.

Open Questions
- Should Track A be only a UI/docs clarification, or should it also add a dedicated `Rebuild Current Setup` control?
- If Track B ever ships, should adoption preserve tracks in place by default or create a new managed layer?
- How much manual confirmation is required before WING routing changes for adopted projects?

Readiness
- Status: needs-design
- Reason: The umbrella issue is still too broad for implementation. Track A is close to ready, but Track B requires a separate design pass before build work starts.
