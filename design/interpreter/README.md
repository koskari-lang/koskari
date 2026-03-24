# Interpreter Architecture

This directory documents the interpreter implementation, including bytecode format, compiler design, and evaluation model.

## Contents

- **bytecode.md**: Bytecode format and opcodes (implemented in `koskari/lib/bytecode.c`)
- **bytecode-metadata-tables.md**: Static metadata tables (location table, extent table with tagged-union KosExtentDesc); O(1) control targets, no forward scans; JUMP locality validation; **implemented** in bytecode.c
- **bytecode-finalization.md**: Final form phase design — collect/size/allocate/emit pipeline, identity-based cpool dedup, unified block with extra table; **design proposal**
- **compiler.md**: Compiler architecture; points to `koskari/compile/` and [SPECIALS.md](../SPECIALS.md). Historical (python-sibilant / CPython bytecode) content is in [archive/compiler-python-sibilant-historical.md](../archive/compiler-python-sibilant-historical.md)
- **evaluator.md**: Evaluation model and drive loop (eval.c); KosCoroutine and run-code-as-coroutine (func.c)
- **[EVALUATOR.md](../EVALUATOR.md)**: Evaluator operation diagrams, phase flow, opcode signaling, exceptions, transfer plans, tail calls, call installs, and play-by-play scenarios
- **execution-invariants.md**: Koskari execution model invariants and design commitments
- **yield-from-execution-model.md**: YIELD_FROM opcode execution model (delegation conduit, KOS_DYN_YIELD_FROM, delegate kinds, PC discipline)
- **introspection-trace.md**: Read-only introspection and trace snapshot design (TraceView; proposed CAPTURE_FRAME opcode is design-only, see that doc §5)
- **callcc.md**: Call/cc implementation design
- **dynamic-node-unified-api.md**: Dynamic node unified API redesign and migration specification (implementation prompt)
- **generic-dynnode-protocol.md**: Generic DynNode interface design, API reference, and type-specific bypass cases (when opcodes use type-named APIs)
- **dynnode-event-model.md**: Event-oriented model for dynamic nodes — lifecycle events (push, wind, resume, handle, unwind, pop, free), evaluator event composition (RETURN/RAISE/YIELD/AWAIT transits), and node behavior categories — **implemented** (see plan/00-gap-analysis.md)
- **evaluator-token-continuation.md**: Evaluator token + continuation application model (KosEvaluatorToken, KoskariContinuationApply)
- **domain.md**: Koskari Domain (ambient dynamic context) — design proposal for parameters and dynamic-extent scoping across AWAIT, boundaries, and continuations
- **gil-cooperation.md**: GIL cooperation design — domain-TLS counter + timer, maybe_cooperate/ensure_gil, candidate call sites, and cost–benefit of options
- **kio-format.md**: `.kio` compiled cache format — binary file layout, softened/hardened serialization levels, constant pool policy, `TaggedTuple`, cache invalidation, and loader algorithm

## Implementation Status

| Component | Status | Files |
|-----------|--------|-------|
| Bytecode storage | Implemented | `bytecode.c`, `bytecode.h`, `opcodes.incl` |
| Opcode definitions | Implemented | `opcodes.incl` |
| Evaluator core | Implemented | `eval.c`, `eval.h` |
| KosCoroutine / run-code-as-coroutine | Implemented | `func.c`, `func.h` |
| Opcode dispatch | Implemented | `eval.c` (most opcodes implemented; CALL_ATTR* stubs remain) |
| Continuation support | Implemented | `eval.c`, `dynchain.c` |
| Compiler | Substantially implemented — see [SPECIALS.md](../SPECIALS.md) | `koskari/compile/compiler.py`, `koskari/compile/specials/` |
| `.kio` cache format | Design only | `design/interpreter/kio-format.md` |
