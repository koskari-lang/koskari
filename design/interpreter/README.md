# Interpreter Architecture

This directory documents the interpreter implementation, including bytecode format, compiler design, and evaluation model.

## Contents

- **bytecode.md**: Bytecode format and opcodes (implemented in `koskari/lib/bytecode.c`)
- **compiler.md**: Compiler architecture and phases
- **evaluator.md**: Evaluation model and ceval loop (implemented in `koskari/lib/eval.c`)
- **execution-invariants.md**: Koskari execution model invariants and design commitments
- **callcc.md**: Call/cc implementation design
- **import-system.md**: Import system implementation

## Implementation Status

| Component | Status | Files |
|-----------|--------|-------|
| Bytecode storage | Implemented | `bytecode.c`, `bytecode.h`, `opcodes.incl` |
| Opcode definitions | Implemented | `opcodes.incl` |
| Evaluator core | Implemented (stub) | `eval.c`, `eval.h` |
| Continuation support | Implemented | `eval.c` |
| Compiler | Not started | - |
