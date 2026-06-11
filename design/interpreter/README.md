# Interpreter Architecture

This directory documents the interpreter implementation, including bytecode format, compiler design, and evaluation model.

## Compiler

The Koskari compiler lives in `koskari/compile/`. Implemented special forms, the macro
pipeline (reader macros, `define-syntax`, **`compile/syntax-*`** /
**`compile/macro-*`**), and compiler optimizations are documented in
[SPECIALS.md](../SPECIALS.md). For the historical python-sibilant (CPython bytecode)
pipeline and sibilant-ceval migration notes, see
[archive/compiler-python-sibilant-historical.md](../archive/compiler-python-sibilant-historical.md).

The former one-page stub [archive/compiler.md](../archive/compiler.md) is archived
(superseded by this section and SPECIALS.md).

## Contents

- **bytecode.md**: Bytecode format and opcodes (implemented in `koskari/lib/bytecode.c`)
- **bytecode-metadata-tables.md**: Static metadata tables (location table, extent table with tagged-union KosExtentDesc); O(1) control targets, no forward scans; JUMP locality validation; **implemented** in bytecode.c
- **bytecode-finalization.md**: Final form phase design — collect/size/allocate/emit pipeline, identity-based cpool dedup, unified block with extra table; **design proposal**
- **evaluator.md**: Evaluation model and drive loop (eval.c); KosCoroutine and run-code-as-coroutine (func.c)
- **[EVALUATOR.md](../EVALUATOR.md)**: Evaluator operation diagrams, phase flow, opcode signaling, exceptions, transfer plans, tail calls, call installs, and play-by-play scenarios
- **iteration-yield-boundaries.md**: YIELD vs YIELD_FROM vs PUSH_FOREACH/ITER_NEXT — implementation map, underlying calls, consolidation proposals
- **opcode-eval-helpers-inventory.md**: Static helpers in `eval.c` and opcode `.c` files — opcode cross-reference, redundancies, suggested public names for native/runtime API
- **execution-invariants.md**: Koskari execution model invariants and design commitments
- **introspection-trace.md**: Read-only introspection and trace snapshot design (TraceView; proposed CAPTURE_FRAME opcode is design-only, see that doc §5)
- **callcc.md**: Call/cc implementation design (current PUSH delimited invoke)
- **delimited-invoke-model.md**: Target delimited invoke — lineage fork, bookmarked return, double-wind fix (not implemented)
- **generic-dynnode-protocol.md**: Generic DynNode interface design, API reference, and type-specific bypass cases (when opcodes use type-named APIs)
- **evaluator-token-continuation.md**: Evaluator token + continuation application model (KosEvaluatorToken, KoskariContinuationApply)
- **domain.md**: Koskari Domain (ambient dynamic context) — design proposal for parameters and dynamic-extent scoping across AWAIT, boundaries, and continuations
- **gil-cooperation.md**: GIL cooperation design — domain-TLS counter + timer, maybe_cooperate/ensure_gil, candidate call sites, and cost–benefit of options
- **kio-format.md**: `.kio` compiled cache format — binary file layout, softened/hardened serialization levels, constant pool policy, `TaggedTuple`, cache invalidation, and loader algorithm

Completed design drafts (historical) live under **[archive/](../archive/)** — e.g.
`dynamic-node-unified-api.md` (partial dynnode migration spec), `compiler.md` (stub),
`dynnode-event-model.md`, `yield-from-execution-model.md`,
`yield-from-revamped-operational-model.md`, `eval-loop-mode-refactor.md`, `block-quasiquote.md`.

## Implementation Status

| Component | Status | Files |
|-----------|--------|-------|
| Bytecode storage | Implemented | `bytecode.c`, `bytecode.h`, `opcodes.incl` |
| Opcode definitions | Implemented | `opcodes.incl` |
| Evaluator core | Implemented | `eval.c`, `eval.h` |
| KosCoroutine / run-code-as-coroutine | Implemented | `func.c`, `func.h` |
| Opcode dispatch | Implemented | `eval.c` (per [OPCODES.md](../OPCODES.md) §1.6) |
| Continuation support | Implemented | `eval.c`, `eval/eval_chain.c`, `dynchain.c`, `dynchain/xfer_plan.c` |
| Compiler | Substantially implemented — see [SPECIALS.md](../SPECIALS.md) | `koskari/compile/compiler.py`, `koskari/compile/specials/` |
| `.kio` cache format | Design only | `design/interpreter/kio-format.md` |
