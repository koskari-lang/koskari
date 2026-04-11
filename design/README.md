# Koskari Design Documentation

This directory contains the design documentation for koskari, organized into three main areas.

**For teaching another system** (human or agent) about what has been done and what must hold, see **[INVARIANTS-AND-CLARIFICATIONS.md](INVARIANTS-AND-CLARIFICATIONS.md)**. It summarizes execution invariants, type and data invariants, C structures, implementation status, and clarifications in one place.

**For open work** (GIL, profiling gaps, compiler and loader backlog, generator wind-at-yield tradeoffs, proposals), see **[TODO.md](TODO.md)**.

## Structure

- **[SPECIALS.md](SPECIALS.md)**: Reference for implemented compiler special forms (compile/bytecode, compile/begin, compile/call, compile/lambda, compile/try, compile/class, compile/define-syntax, import, import-from, and many more; see SPECIALS.md for full list), with use-cases and examples
- **[MACROS.md](MACROS.md)**: Reference for implemented macros (set!, ref, etc.) defined in basics.ki; format mirrors SPECIALS.md
- **language/**: Language features, syntax, special forms, operators, and built-in constructs
- **api/**: Runtime API, types, functions, interfaces, and [package layout and import policy](api/package-layout.md); [Python inspect interop for native callables](api/inspect-interop-kos-callables.md) (`KosFunction`, `KosBoundMethod`)
- **[EVALUATOR.md](EVALUATOR.md)**: Evaluator operation—phase flow diagrams, opcode signaling, exceptions, transfer plans, tail calls, call installs, play-by-play scenarios
- **interpreter/**: Interpreter architecture, bytecode format, compiler design,
  evaluation model, continuation application (including evaluator-token model),
  and [Koskari Domain](interpreter/domain.md) (ambient dynamic context, design proposal)
- **Proposals**: [fenced-foreign-literal.md](fenced-foreign-literal.md) — triple-backtick fenced foreign literals (JSON, etc.) lowered at compile time via `compile/apply-fence`; [bundles-and-namespace-facets.md](bundles-and-namespace-facets.md) — bundles as namespace facets for cohesive feature delivery with export facades and compile-time activation; [switch-eq-opcodes.md](switch-eq-opcodes.md) — SWITCH_EQ opcode family and `compile/switch` (proposal)
- **[ideas.md](ideas.md)**: Potential concepts and options (e.g. feature flag for demand vs run_sync imports) — not committed plans, just notes to revisit

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
