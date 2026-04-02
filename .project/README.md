# .project

This folder holds repo-local agent and workflow context.

Use it for:

- `.project/PROJECT_CONTEXT.md`
  Human-readable project rules, constraints, and durable context for future agents.
- `.project/project-helper-config.json`
  Machine-readable tooling, platform, and integration metadata.

Do not use this folder for:

- secrets
- access tokens
- passwords
- private keys
- large generated files
- normal product or engineering docs that belong in `docs/`

Rules:

- keep files short and stable
- persist durable identifiers and workflow metadata, not transient notes
- prefer one canonical file per concern
- update these files when project rules or integrations materially change
