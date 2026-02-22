# Interpreter Architecture

This directory documents the interpreter implementation, including bytecode format, compiler design, and evaluation model.

## Contents

- **bytecode.md**: Bytecode format and opcodes (implemented in `koskari/lib/bytecode.c`)
- **bytecode-metadata-tables.md**: Static metadata tables (location table, extent table with tagged-union KosExtentDesc); O(1) control targets, no forward scans; JUMP locality validation; **implemented** in bytecode.c
- **compiler.md**: Compiler architecture and phases
- **evaluator.md**: Evaluation model and drive loop (eval.c); KosCoroutine and run-code-as-coroutine (func.c)
- **execution-invariants.md**: Koskari execution model invariants and design commitments
- **yield-from-execution-model.md**: YIELD_FROM opcode execution model (delegation conduit, KOS_DYN_YIELD_FROM, delegate kinds, PC discipline)
- **introspection-trace.md**: Read-only introspection and trace snapshot design (TraceView, KOS_OP_CAPTURE_FRAME, exception integration)
- **callcc.md**: Call/cc implementation design
- **dynamic-node-unified-api.md**: Dynamic node unified API redesign and migration specification (implementation prompt)
- **evaluator-token-continuation.md**: Evaluator token + continuation application model (KosEvaluatorToken, KoskariContinuationApply)
- **domain.md**: Koskari Domain (ambient dynamic context) — design proposal for parameters and dynamic-extent scoping across AWAIT, boundaries, and continuations
- **kio-format.md**: `.kio` compiled cache format — binary file layout, softened/hardened serialization levels, constant pool policy, `TaggedTuple`, cache invalidation, and loader algorithm

## Implementation Status

| Component | Status | Files |
|-----------|--------|-------|
| Bytecode storage | Implemented | `bytecode.c`, `bytecode.h`, `opcodes.incl` |
| Opcode definitions | Implemented | `opcodes.incl` |
| Evaluator core | Implemented | `eval.c`, `eval.h` |
| KosCoroutine / run-code-as-coroutine | Implemented | `func.c`, `func.h` |
| Opcode dispatch | Implemented | `eval.c` (many opcodes implemented; many still stubs) |
| Continuation support | Implemented | `eval.c`, `dynchain.c` |
| Compiler | Not started | - |
| `.kio` cache format | Design only | `design/interpreter/kio-format.md` |
