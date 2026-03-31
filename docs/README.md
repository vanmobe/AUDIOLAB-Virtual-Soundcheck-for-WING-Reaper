# Documentation Map

Use this page as the starting point for repository documentation.

## End Users

- [../INSTALL.md](../INSTALL.md) - installer-first setup for macOS and Windows
- [../QUICKSTART.md](../QUICKSTART.md) - shortest path from install to first WING connection
- [USER_GUIDE.md](USER_GUIDE.md) - day-to-day operation in REAPER with a Behringer WING
- [CC_BUTTONS_AND_AUTO_TRIGGER.md](CC_BUTTONS_AND_AUTO_TRIGGER.md) - MIDI CC control, auto-trigger, and SD recording notes

## Developers

- [../SETUP.md](../SETUP.md) - build environment, local install, and packaging entry points
- [ARCHITECTURE.md](ARCHITECTURE.md) - module boundaries and runtime flow
- [WING_OSC_PROTOCOL.md](WING_OSC_PROTOCOL.md) - implemented OSC behavior and reference notes

## Additional Reference

- [ISSUE_13_EXISTING_PROJECT_SOUNDCHECK_INTAKE.md](ISSUE_13_EXISTING_PROJECT_SOUNDCHECK_INTAKE.md) - intake notes and recommended split for existing-project soundcheck workflows
- [ISSUE_16_EXISTING_REAPER_PROJECT_ADOPTION_INTAKE.md](ISSUE_16_EXISTING_REAPER_PROJECT_ADOPTION_INTAKE.md) - intake notes for adopting an imported REAPER project into a managed WING workflow
- [ISSUE_16_EXISTING_REAPER_PROJECT_ADOPTION_PROPOSAL.md](ISSUE_16_EXISTING_REAPER_PROJECT_ADOPTION_PROPOSAL.md) - proposed v1 action boundary and workflow for imported-project adoption
- [ISSUE_16_PHASE3_ADOPT_IN_PLACE_DECISION_MEMO.md](ISSUE_16_PHASE3_ADOPT_IN_PLACE_DECISION_MEMO.md) - criteria for stopping at a safe managed-layer workflow versus pursuing in-place adoption
- [ISSUE_16_MANUAL_MAPPING_AND_ORDERING_INTAKE.md](ISSUE_16_MANUAL_MAPPING_AND_ORDERING_INTAKE.md) - intake notes for editable track-to-channel assignment, ordering, and USB/CARD choice inside imported-project adoption
- [ISSUE_16_MANUAL_MAPPING_AND_ROUTING_PROPOSAL.md](ISSUE_16_MANUAL_MAPPING_AND_ROUTING_PROPOSAL.md) - concrete proposal for editable adoption mapping, ordering, conflict handling, and mirrored routing
- [WING_SELECTED_CHANNEL_BRIDGE.md](WING_SELECTED_CHANNEL_BRIDGE.md) - current action split, scope, and next steps for the standalone bridge
- [WING_SELECTED_CHANNEL_SUPERRACK_BRIDGE_PLAN_VALIDATION.md](WING_SELECTED_CHANNEL_SUPERRACK_BRIDGE_PLAN_VALIDATION.md) - validation notes for the selected-channel bridge plan
- [ISSUE_14_SELECTED_CHANNEL_BRIDGE_IMPLEMENTATION_PROPOSAL.md](ISSUE_14_SELECTED_CHANNEL_BRIDGE_IMPLEMENTATION_PROPOSAL.md) - implementation proposal for issue #14 selected-channel bridge delivery
- [../snapshots/README.md](../snapshots/README.md) - snapshot and legacy/manual MIDI mapping notes
- [../releases/README.md](../releases/README.md) - packaged release artifacts in this repo

## Suggested Reading Order

1. Install with [../INSTALL.md](../INSTALL.md).
2. Connect and verify with [../QUICKSTART.md](../QUICKSTART.md).
3. Use [USER_GUIDE.md](USER_GUIDE.md) during sessions.
4. Open the focused reference guides only when you need deeper behavior details.
