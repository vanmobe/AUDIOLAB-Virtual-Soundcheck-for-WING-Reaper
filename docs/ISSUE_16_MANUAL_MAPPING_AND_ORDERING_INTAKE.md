# Issue #16 – Manual Mapping, Ordering, and Output-Mode Intake

Issue
- Extend imported-project adoption so the operator can reorder mappings before apply, explicitly choose which REAPER track lands on which WING channel, and explicitly choose the playback routing plan, including mode (`USB` or `CARD`) and exact slot assignment, before apply.

Problem
- The current adoption flow is still effectively auto-mapped. It scans the imported REAPER project, proposes likely WING channel matches, and then applies them with minimal operator control.
- That is not sufficient for real sessions where:
- track order in REAPER differs from desired WING channel order
- the operator wants to move a REAPER track to a different WING channel even when the name match is “good”
- stereo or name-paired tracks need to move together, but not necessarily to the next available named match
- the operator wants to choose or review `USB` vs `CARD` for this adoption before any write
- the operator wants to choose not just mode, but where each adopted source is routed inside that mode
- input and output routing should remain mirrored, so if a track is assigned to `USB 9-10`, its REAPER record input and hardware output should both follow `USB 9-10`
- the current setup engine allocates playback slots from the selected-source order, so if the operator cannot control the confirmed order or the exact slot target, the resulting USB/CARD bank layout is only indirectly controllable

Proposed Direction
- Keep this inside the separate adoption action, not the main managed setup dialog.
- Add an explicit pre-apply mapping editor step with four operator controls:
- assign each adoptable REAPER track to a chosen WING channel
- control confirmed mapping order at both levels:
- channel level: which REAPER track lands on which WING channel
- source/output level: which playback slot or pair is used for the mirrored REAPER input/output route
- choose the adoption output mode (`USB` or `CARD`) inside the adoption workflow before apply
- choose the exact playback slot target for each adopted mapping, with REAPER input and hardware output mirrored to that same slot or pair
- Treat this as a guided editor, not a free-form text parser in the first user-facing design.

User decisions captured in intake:
- Reordering applies at both the channel level and the source/output routing level.
- `USB`/`CARD` remains a global apply choice for the adoption run, not a per-channel setting.
- The current automatic routing proposal should still be shown first, but the operator can keep or override it.
- Explicit playback-slot override is optional, not required.
- Stereo odd-start slot rules stay enforced; stereo rows may only target valid odd-start pairs.
- Duplicate playback slots should not be allowed by the UI.
- Windows fallback should follow the same style as the current soundcheck action fallback rather than being withheld entirely.
- macOS editor direction: use a table with one row per adoptable track/channel mapping and dropdown controls for configurable fields.
- Dropdown behavior direction:
- preferred behavior: show all options but gray out options that are not currently selectable
- acceptable alternative: allow conflicting picks temporarily, but show conflicts explicitly and block apply until all are resolved

Recommended v1 workflow:
1. Run `Extensions -> WINGuard: Adopt Existing REAPER Project for Soundcheck`.
2. Connect to WING and scan the project as today.
3. Show a mapping review/edit step with rows like:
   - `REAPER track`
   - `stereo/mono`
   - `suggested WING channel`
   - `final WING channel`
   - `routing mode`
   - `playback slot` (`USB 9`, `USB 9-10`, `CARD 5`, `CARD 5-6`, etc.)
   - `mirrored REAPER I/O preview`
   - `status / conflict indicator`
4. Let the operator move rows up/down or otherwise reorder the confirmed mapping set.
5. Let the operator override the suggested WING channel per row.
6. Let the operator choose `USB` or `CARD` for this apply.
7. Let the operator keep the auto-assigned slot plan or optionally override individual playback slots.
8. Recompute and preview the resulting playback-slot plan before final confirmation.
9. Validate conflicts before apply:
   - duplicate WING channel targets
   - duplicate playback slots
   - stereo track assigned to incomplete or even-start slot pair
   - requested slot outside the active `USB`/`CARD` bank
8. Apply in place only after explicit confirmation.

Alternatives
- Reuse the main `Configure Virtual Soundcheck/Recording` source selector for adoption
- Rejected because that flow assumes WINGuard-managed sources, not imported-track-to-channel remapping
- Add only a “sort by REAPER order” toggle and keep the rest automatic
- Rejected because the user need is stronger: specific track-to-channel reassignment, not just bulk ordering
- Add only a `USB`/`CARD` selector without manual mapping
- Rejected because output mode alone does not solve channel reassignment or stereo move cases
- Add manual WING channel reassignment but keep playback-slot allocation automatic
- Rejected because the user explicitly wants routing control, not only channel remap control
- Free-form text mapping spec in the fallback UI as the primary design
- Rejected for primary UX because it is too error-prone for stereo, reordering, and partial-match review

Checks
- Architecture: Best fit is still the separate adoption action. The current `ShowExistingProjectAdoptionDialog()` path in [src/ui/dialog_bridge.cpp](../src/ui/dialog_bridge.cpp) is too summary-driven; it needs an intermediate mapping model rather than jumping from suggestions to `SetupSoundcheckFromSelection()`. The current engine only understands `SourceSelectionInfo` plus sequential allocation; it does not have a first-class “adoption routing plan” object.
- UX: The operator mental model should be “review and edit the adoption plan before WINGuard writes anything.” Auto-suggestions are still useful, but they need to become editable defaults, not the final mapping. The UI must distinguish three different things clearly: WING channel target, routing mode, and playback slot.
- UI/UX: macOS should use a dedicated table-based editor view or sheet with one row per adoptable mapping and dropdowns for editable fields. The UI should either gray out unavailable targets directly in those dropdowns or allow temporary conflicts with explicit per-row conflict state and an apply gate. Windows should follow the same fallback style as the current soundcheck action: REAPER dialogs with structured text input/review, not a hidden or unavailable path.
- Compatibility: Backward-compatible if this remains scoped to the adoption action. Existing managed-project rebuild behavior should remain unchanged. Existing adopted-in-place tracks need a predictable re-adopt/reorder story.
- API/Data: Current `SourceSelectionInfo` is not enough to represent “REAPER track X is being adopted onto WING channel Y with routing mode M and playback slot S.” A dedicated adoption-plan row structure is likely needed, with track reference, chosen channel, stereo intent, confirmed order, chosen global mode, and explicit slot start/end when overridden. Because `USB`/`CARD` is still a global setting, the design must decide whether adoption temporarily stages that global value before apply or explicitly commits it as part of confirmation.
- Risk: The main risks are destructive remapping, conflicting channel assignments, conflicting playback slots, stereo pair splits, and operator confusion about whether reordering changes WING channel identity, playback-slot order, or both. The design must keep those separate and visible.
- Risk: The main risks are destructive remapping, conflicting channel assignments, conflicting playback slots, stereo pair splits, and operator confusion about whether reordering changes WING channel identity, playback-slot order, or both. The design must keep those separate and visible. Allowing temporarily conflicting dropdown states is acceptable only if apply is hard-blocked until the table has zero conflicts.
- Testing: Manual coverage needs to include reordered mono tracks, moved stereo tracks, mixed mono/stereo with gap-fill behavior, explicit slot overrides, `USB` and `CARD` apply, duplicate-channel conflict handling, duplicate-slot conflict handling, partially already-managed projects, and cancellation before apply.

Open Questions
- What is the safest row model for stereo tracks:
- one row per REAPER track, even when stereo
- one row per logical WING channel target, with stereo preview attached
- If a user moves a stereo imported track onto `CH8`, should the UI preview “CH8 -> USB9-10” directly so the odd-start rule stays visible?
- Should the adoption flow temporarily stage `USB`/`CARD` mode without immediately rewriting `config.soundcheck_output_mode`, or is changing global mode during adoption acceptable?
- What should happen when the imported project already contains some adopted/managed channels and the operator wants to reorder only part of the set?
- What exact fallback input format should be used on Windows so it stays consistent with the current soundcheck action while still preventing duplicate-slot conflicts?
- Should the macOS dropdowns gray out unavailable options immediately, or should the table allow temporarily invalid combinations and rely on conflict badges plus an apply gate?

Readiness
- Status: needs-design
- Reason: Product intent is now much clearer, and the editor direction is also clearer, but the implementation shape is still not stable enough for build. The codebase currently has no explicit adoption-plan model, no table-based mapping editor, no explicit playback-slot override model, and no UI contract for separating channel assignment, global mode choice, and mirrored slot routing. The remaining work is design-shape definition, not problem discovery.

Recommendation
- Treat this as the next design slice for `#16`, not a small extension of the current auto-suggest flow.
- Settle these decisions before build:
- mapping editor interaction model
- whether `USB`/`CARD` is staged per adoption run or written globally up front
- explicit slot-override model and conflict rules
- row model for stereo tracks
- conflict rules for duplicate or partially managed channel assignments
- mirrored input/output routing constraints
- fallback-platform strategy
