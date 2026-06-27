# Design ideas and potential concepts

Notes on possible future changes or options. Not committed plans; just things to
revisit or prototype.

## Import path selection

The loader currently uses the demand path (`exec_module_demand`) by default;
switching to the async path is a one-line change in `exec_module` (see
[api/import-system.md](api/import-system.md)).

**Idea:** Add a feature flag (e.g. environment variable, or a knob on the
loader/package) to choose at runtime between demand imports and run_sync
imports, instead of editing the return line in code. Would allow testing or
deploying either path without a code change.

## (More ideas)

Add further potential concepts below as needed.

## koskari_scripts (optional follow-ups)

v1 **`koskari_scripts`** packaging is implemented. Optional hardening work:

- End-to-end pip-install integration test (build wheel, install in venv, run
  installed command).
- Native binary dispatch for **`koskari_scripts`** entry points (Phase A native
  **`koskari`** embed exists via **`make koskari-bin`**; not wired as installed
  command shebang).

See [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md).
