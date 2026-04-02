# Project Context

## System Purpose

- WINGuard is a C++17 REAPER extension that connects to a Behringer WING console over OSC/UDP.
- Primary outcomes are stable REAPER extension behavior, reliable channel sync, virtual soundcheck routing, recorder helpers, and selected-channel bridge support.
- The product is embedded in REAPER, so host stability and predictable operator workflow matter more than broad feature expansion.

## Major Subsystems

- `src/extension/`: REAPER lifecycle, command registration, runtime orchestration, managed-source monitoring.
- `src/core/`: WING OSC transport, query/reply handling, routing, and protocol-specific behavior.
- `src/track/`: REAPER track creation, updates, and sync from WING data.
- `src/utilities/`: config, logging, platform helpers, string helpers.
- `src/ui/`: native macOS dialogs and Windows dialog/bridge surfaces.

## Architectural Constraints

- Keep extension, core, track, config, and UI boundaries intact; prefer small targeted changes over refactors.
- Do not migrate away from JSON config persistence unless explicitly requested; runtime config is loaded and saved through `src/utilities/wing_config.cpp`.
- Keep WING protocol behavior aligned with implementation truth and `docs/WING_OSC_PROTOCOL.md`.
- Preserve REAPER extension stability first; do not let optional integrations break transport or connection flows.

## Product And Workflow Constraints

- The main user flow is scan/select/manual-IP, connect, fetch/sync tracks, then optionally configure soundcheck, recorder helpers, MIDI actions, and auto-trigger.
- Virtual soundcheck and selected-channel bridge are intentionally separate workflows.
- Buses and matrices may participate in record-only flows; do not assume they are full soundcheck-capable sources without verifying implementation and docs.
- Managed-channel source monitoring and soundcheck-mode sync are expected to preserve existing prepared projects rather than rebuild them implicitly.

## Platform And Host Fit

- This product is a REAPER desktop plugin, not a standalone app or web service.
- macOS UI uses native Objective-C++ dialogs.
- Windows uses the native Win32 main dialog routed from the dialog bridge.
- Prefer host-native platform behavior over introducing a custom cross-platform UI framework.

## Supported Platforms And Parity Rules

- Officially supported platforms are macOS and Windows.
- `CMakeLists.txt` explicitly fails Linux and other non-macOS/non-Windows builds.
- Cross-platform parity is expected for the main connection and REAPER setup workflow.
- Platform-specific UI implementations are allowed, but feature behavior should stay aligned unless a difference is explicitly documented as intentional.
- Validation should include successful build on the changed platform and manual REAPER verification when runtime behavior changes.

## API And Data Conventions

- WING OSC is fixed at port `2223` in current runtime behavior.
- Discovery still uses the WING handshake probe on UDP `2222`.
- Config compatibility is centered on `config.json`; legacy listener-port `2224` is migrated in memory and rewritten to `2223`.
- Persistent REAPER track metadata keys such as `P_EXT:WINGCONNECTOR_SOURCE_ID` and `P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE` are compatibility-sensitive and should be treated as durable contracts.

## Security, Compliance, And Observability Expectations

- The plugin communicates with WING over local-network UDP and stores operator settings in local JSON config only.
- There is no evident special compliance regime in repo context, but changes should avoid expanding trust boundaries casually.
- Preserve operator diagnosability through REAPER-visible status/log messaging, especially for connection and routing failures.

## Release And Rollout Norms

- Keep compatibility with existing prepared REAPER projects and existing `config.json` content when possible.
- Tagged releases are packaged through GitHub Actions and platform packaging scripts.
- Non-trivial changes should meet the repo minimum validation bar of a successful build.
- After changes that produce a new plugin binary, rebuild and install the plugin into the REAPER `UserPlugins` path unless the user explicitly says not to.

## GitHub Delivery Workflow

- Repository: `vanmobe/AUDIOLAB-Virtual-Soundcheck-for-WING-Reaper`.
- GitHub Actions are used for CI and tagged release packaging.
- GitHub Project delivery metadata lives in `.project/github-project-config.json`.
- The active delivery project is `Wing Reaper Integration` (project number `4` under `vanmobe`).

## Testing And Validation Norms

- There is no strong automated regression suite; avoid speculative changes that cannot at least be build-verified.
- Minimum validation after non-trivial changes is a successful platform build.
- Runtime-sensitive changes should also be checked manually inside REAPER against a reachable WING or a realistic operator setup.
- When compatibility-sensitive behavior changes, verify existing config loading, managed track metadata reuse, and cross-platform dialog flow expectations.

## Known Non-Goals

- Do not flatten the module structure into a single mixed layer.
- Do not replace `config.json` persistence with a new storage model without explicit direction.
- Do not treat unverified WING OSC paths as supported product behavior just because they exist in reference material.
- Do not expand Linux support based on older fallback-language in docs or intake notes; current build support is macOS and Windows only.
