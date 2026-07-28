# Koskari Design Documentation

This directory contains the design documentation for koskari, organized into three main areas.

**For teaching another system** (human or agent) about what has been done and what must hold, see **[INVARIANTS-AND-CLARIFICATIONS.md](INVARIANTS-AND-CLARIFICATIONS.md)**. It summarizes execution invariants, type and data invariants, C structures, implementation status, and clarifications in one place.

**For open work** (GIL, profiling gaps, compiler and loader backlog, proposals), see **[TODO.md](TODO.md)**.

## Structure

- **[SPECIALS.md](SPECIALS.md)**: Reference for implemented compiler special forms (compile/bytecode, compile/begin, compile/call, compile/lambda, compile/function, compile/try, compile/class, compile/define-syntax, import, import-from, and many more; see SPECIALS.md for full list), with use-cases and examples
- **[MACROS.md](MACROS.md)**: Reference for implemented macros (set!, ref, etc.) defined in basics.ki; format mirrors SPECIALS.md
- **[OPCODES.md](OPCODES.md)**: Canonical reference for implemented opcodes
- **[LAYOUTS.md](LAYOUTS.md)**: Layout handler rollup; normative layout syntax graduated to [docs/reference/reader-layout.md](../docs/reference/reader-layout.md)
- **[PRAGMA.md](PRAGMA.md)**: Module pragma rollup — how to set a pragma, the available `reader/*`, `compiler/*`, and `pragma/*` settings, and the model (registry, watchers, meta-pragma)
- **language/**: Language features, syntax, special forms, operators, and built-in constructs
- **api/**: Runtime API, types, functions, interfaces, and [package layout and import policy](api/package-layout.md)
- **[EVALUATOR.md](EVALUATOR.md)**: Evaluator operation—phase flow diagrams, opcode signaling, exceptions, transfer plans, tail calls, call installs, play-by-play scenarios
- **[interpreter/eval-state-live-authority.md](interpreter/eval-state-live-authority.md)**: `KosEvalState` live authority during RUN (Phase 1 landed); Tier-2 xfer incremental retarget; see [archive/kos_eval_state_api_migration.md](archive/kos_eval_state_api_migration.md) (archived opcode migration ledger)
- **interpreter/**: Interpreter architecture, bytecode format, compiler design,
  evaluation model, continuation application (including evaluator-token model),
  and [Koskari Domain](archive/domain.md) (ambient dynamic context; implemented, archived with [domain-task-isolation.md](archive/domain-task-isolation.md))
- **Proposals**: [fenced-foreign-literal.md](fenced-foreign-literal.md) — triple-backtick fenced foreign literals (JSON, etc.) lowered at compile time via `compile/apply-fence`; [bundles-and-namespace-facets.md](bundles-and-namespace-facets.md) — bundles as namespace facets for cohesive feature delivery with export facades and compile-time activation; [koskari-lint.md](koskari-lint.md) — advisory `koskari.lint` validator (a `koskari-extras` reader + non-evaluating validate pass, reusing the real reader and compile-time matcher builders); [suffix-macros.md](suffix-macros.md) — suffix reader macro registry and driver (replacing the `post_value_hooks` chain), showcased by pragma-gated dotted attributes on any value and trailing-hyphen string segments
- **Archived**: [archive/koskari-scripts-packaging.md](archive/koskari-scripts-packaging.md) — v1 **`koskari_scripts`** entry points, **`koskari.extras.setuptools`**, and **`koskari.launcher`** for installed CLI applications
- **Layout defaults**: [language/flat-layout-default.md](language/flat-layout-default.md) — default **`layout/block`** flat splice and **`layout/block-begin`** opt-in wrap
- **[documentation-goals.md](documentation-goals.md)**: Meta-spec for future user-facing `docs/` tree (audiences, IA, maintenance)
- **[corpus-schema-philosophy.md](corpus-schema-philosophy.md)**: Language conformance corpus schema, verification model, and Hugging Face dataset views derived from corpus records
- **Archived**: [archive/corpus-mcp-server.md](archive/corpus-mcp-server.md) — HF dataset publication (Track A) and Koskari MCP container server over the Dataset Viewer API (Track B); both implemented (server: [tools/mcp/README.md](../tools/mcp/README.md))
- **archive/**: Completed implementation specs and superseded proposals (e.g. demand-evalex phase 1–2, eval-result unification, kos_eval_state migration, interrupted-transfer-plans, bytecode-metadata-tables, node-owned-operand-stack, let-syntax-and-inline-macro, koskari-scripts-packaging, switch-eq-opcodes, bracket-pair, EXTRA table / CALL_FAST\*, parameterized arithmetic, function annotations opcode, inspect interop, defer/Task/Future matrix, opcode-access inventory, Domain / parameter task isolation)
- **[docs-tracker.md](docs-tracker.md)**: User documentation site status (correlates documentation-goals and corpus plans with `docs/` progress)
- **[ideas.md](ideas.md)**: Potential concepts and options (e.g. feature flag for demand vs run_sync imports) — not committed plans, just notes to revisit
- **[expressive-facets.md](expressive-facets.md)**: Exploratory survey of language facilities Koskari does not have and could claim cheaply, because the continuation and dynamic-extent machinery already exists — effect handlers, conditions/restarts, structured concurrency, supervision trees, contracts with blame, transducers, `amb`, re-entrant debugging, capability-scoped evaluation. Each with sketches, open questions, and reading

## Purpose

This design documentation catalogs:
1. Features from `python-sibilant` (CPython bytecode target)
2. Features from `sibilant-ceval` (custom bytecode evaluator)
3. Planned features and architectural decisions for `koskari`

Each feature is marked with its origin and migration status to help guide implementation.

## Migration Progress

The following components have been fully migrated with native C extensions:

| Component | Origin | Status |
|-----------|--------|--------|
| symbol, keyword | python-sibilant | Complete |
| pair, nil, cons | python-sibilant | Complete |
| values | python-sibilant | Complete |
| gensym | python-sibilant | Complete |
| Namespace, ModuleNamespace | sibilant-ceval | Complete |
| Ref types (6 variants) | sibilant-ceval | Complete |
| Formals, FormalsSpec | sibilant-ceval | Complete |
| Parser (Reader, SourceStream) | python-sibilant | Complete |
| Compiler | python-sibilant | Partial — see [SPECIALS.md](SPECIALS.md) for the full implemented list |
| Evaluator | sibilant-ceval | Complete |

See [api/types.md](api/types.md) for detailed type documentation.
