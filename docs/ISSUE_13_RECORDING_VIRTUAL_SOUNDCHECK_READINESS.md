# Issue #13 – Recording / Virtual Soundcheck Readiness

Issue
- Resume or rebuild a virtual soundcheck setup when the REAPER project already exists before the plugin connects to WING.

Problem
- The current workflow is optimized for connecting to WING, discovering sources, and then applying a managed recording/soundcheck setup.
- Issue #13 mixes two different operator needs:
- resume a previously plugin-managed project on a later day and confirm it is ready
- rebuild WING routing from an existing REAPER session, including sessions created elsewhere

Proposed Direction
- Split the issue into a buildable v1 and a deferred v2.
- V1 should focus on previously plugin-managed projects only:
- detect existing managed tracks via persisted source ids and existing track naming
- expose a deliberate `Rebuild Current Selection` or equivalent staged action that reuses the current managed selection and reapplies WING routing for the selected `USB` or `CARD` mode
- keep the existing live setup validation flow as the confirmation step after reconnect
- V2, if still desired later, should be a separate issue for inferring WING routing from arbitrary imported REAPER projects that were not created by this plugin

Alternatives
- Infer channel mapping from arbitrary REAPER track names and I/O only
- Rejected for now because naming and routing are not stable enough to treat as authoritative, especially for imported sessions
- Auto-reapply routing immediately on connect
- Rejected for now because it would make connection side effects more aggressive and less predictable in a stability-sensitive REAPER extension

Checks
- Architecture: Fits the current split. Reuse `ReaperExtension::GetAvailableSources()`, `SetupSoundcheckFromSelection()`, and validation in `src/extension/reaper_extension.cpp` without moving routing logic out of extension/core boundaries.
- UX: A dedicated staged rebuild action is clearer than expecting users to infer that reconnect validation alone means the setup can be rebuilt safely.
- UI/UX: The macOS staged setup flow already supports reusing an existing selection and changing output mode. A small explicit rebuild affordance is a better fit than inventing a new wizard.
- Compatibility: Safe if limited to plugin-managed tracks and existing persisted source ids. Arbitrary-project inference would create backward-compatibility and trust issues.
- API/Data: No config migration is required beyond continuing to rely on `last_selected_source_ids` and managed track ext state.
- Risk: The main risk is accidentally rebuilding from ambiguous project state. Restricting v1 to managed tracks keeps the source of truth explicit.
- Testing: Minimum bar is a successful build plus manual validation of three flows: fresh setup, reconnect-and-validate existing managed setup, and rebuild current managed selection in both `USB` and `CARD` modes.

Open Questions
- Should the rebuild action replace all tracks, or only rebuild WING routing while preserving an existing imported track layout?
- On non-macOS fallback UI, is the rebuild action a distinct prompt choice or just a documented selection mode?
- Should reconnect ever offer a passive reminder that a managed setup can be rebuilt, without auto-applying changes?

Readiness
- Status: needs-design
- Reason: The issue is not ready as written because it combines a safe managed-project rebuild path with a much broader arbitrary-project reconstruction idea. The managed-project slice is close to ready, but the exact user-facing rebuild action and track-replacement behavior still need to be fixed before build work starts.
