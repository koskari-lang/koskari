# KOS_OP_THEEND
- **Style**: NOARG.
- **Stack**: Unreachable for normal execution.
- **Invariants**: If execution reaches THEEND, the VM raises RuntimeError. THEEND must always be last in opcodes.incl.

**Example:** Sentinel at end of code; no legitimate execution path reaches it.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | RETURN | 1   | 1        | normal exit |
| 1     | THEEND | 0   | 0        | unreachable; VM raises if pc lands here |


---

## See also

- [VM overview](../index.md) — notation, invariants, and opcode catalog
- `koskari/lib/opcodes.incl` — Authoritative opcode enum and order
- `koskari.dis` — Runtime disassembly of KosCode objects
