# KOS_OP_THEEND

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: No runtime implementation.
- **Stack**: Unreachable for normal execution.
- **Invariants**: If execution reaches THEEND, the VM raises RuntimeError. THEEND must always be last in opcodes.incl.

**Example:** Sentinel at end of code; no legitimate execution path reaches it.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | RETURN | 1   | 1        | normal exit |
| 1     | THEEND | 0   | 0        | unreachable; VM raises if pc lands here |


---

## See also

- [interpreter/bytecode.md](../../../design/interpreter/bytecode.md) — Bytecode format, KosCode layout, API
- [interpreter/bytecode-metadata-tables.md](../../../design/interpreter/bytecode-metadata-tables.md) — Extents, location table, jump locality
- [interpreter/execution-invariants.md](../../../design/interpreter/execution-invariants.md) — Execution model
- [interpreter/evaluator.md](../../../design/interpreter/evaluator.md) — Evaluation loop
- [FEATURES.md](../../../design/FEATURES.md) — Implementation status and feature list
- `koskari/lib/opcodes.incl` — Authoritative opcode enum and order
- `koskari.dis` — Runtime disassembly of KosCode objects


## Document status

This document is the design target for Koskari opcodes. When semantics or
invariants change, update this file and the implementation together. Last
substantive update: pre/post-compile representation (§1.5–1.10), LABEL/POSITION
stripping, quick-reference index, glossary, maintaining checklist, and
intended/current notes for stubs.

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
