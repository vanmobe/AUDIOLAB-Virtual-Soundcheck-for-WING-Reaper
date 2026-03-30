# Issue #16 – Existing REAPER Project Adoption Intake

Issue
- Adopt an existing REAPER project into a managed WING soundcheck workflow.

Problem
- The current WINGuard flow assumes WING is the source of truth and that WINGuard either creates or already manages the relevant tracks.
- That breaks down when a user opens a project from someone else, or a project that mixes raw inputs, folders, stems, buses, matrices, FX returns, and utility tracks.
- Treating arbitrary REAPER tracks as authoritative inside the main soundcheck flow would be too destructive and too ambiguous.

Proposed Direction
- Integrate this as a separate REAPER action and guided adoption workflow, not as an extra branch inside `Configure Virtual Soundcheck/Recording`.
- Recommended action boundary:
- `WINGuard: Adopt Existing REAPER Project for Soundcheck`
- Recommended workflow:
- connect to WING first and fetch channel metadata
- scan REAPER tracks and build candidate adoptable inputs
- propose mappings, but keep them advisory until explicit confirmation
- let the user choose one of two modes:
- `Adopt In Place`: preserve items, FX, folders, and layout while attaching managed source ids and rewriting only the agreed track I/O
- `Create Managed Layer`: leave the imported project intact and create a new WINGuard-managed recording layer beside it
- show a confirmation summary before any routing or metadata write
- only after confirmation should WINGuard write `P_EXT:WINGCONNECTOR_SOURCE_ID`, update `last_selected_source_ids`, and apply WING routing

Alternatives
- Add adoption as another mode in the main dialog
- Rejected because the main dialog is now optimized for managed resume/rebuild, where WING remains the source of truth and the user expects low-risk validation plus controlled rebuilds
- Automatically infer and apply mappings with no review step
- Rejected because stereo, buses, stems, and near-match names make wrong mappings too likely
- Support only in-place adoption
- Rejected because some users will need to preserve the imported project unchanged and create a separate managed layer

Checks
- Architecture: Best fit is a separate action boundary, similar to the selected-channel bridge split. The main soundcheck action should stay focused on managed setup, validation, and rebuild. Adoption needs its own workflow state and mapping model.
- UX: The user goal is not “rebuild what WINGuard already owns.” It is “review what can be adopted safely.” That needs a guided flow with explicit confirmation, unmatched-track handling, and a preservation choice.
- UI/UX: macOS can support a dedicated adoption wizard. Windows/Linux fallback should stay simpler at first, likely summary-and-confirm rather than a dense inline editor.
- Compatibility: Backward-compatible if adoption only writes managed ids to tracks the user explicitly accepts. Existing managed projects should never silently switch into adoption behavior.
- API/Data: Likely needs a small adoption-specific config/state section if the workflow stores pending mappings or remembers whether tracks were adopted in place vs created as a managed layer.
- Risk: Highest risks are destructive rewrites, incorrect stereo handling, and accidentally adopting non-input tracks. A confirm-everything workflow and an opt-in managed layer mode reduce that risk.
- Testing: Needs a manual matrix covering mixed projects, stereo mismatches, buses/matrices, partially matched names, unchanged imported projects, and rollback-safe cancel paths.

Open Questions
- What counts as a candidate adoptable track by default: armed tracks, tracks with hardware inputs, name matches, or a broader scan?
- Should the first version support buses and matrices, or only channel strips?
- Should `Adopt In Place` preserve the existing track name by default, or rename to the WING source name after adoption?
- How should the fallback UI work on Windows/Linux if the mapping review is too complex for a single REAPER input dialog?
- Does adoption need a “dry run” preview that never writes anything, even temporarily?

Readiness
- Status: needs-design
- Reason: The problem and safe direction are clear, but the workflow still needs a stable v1 scope: candidate-track rules, mapping review UX, supported source families, and whether adoption-in-place or managed-layer creation should be the default.
