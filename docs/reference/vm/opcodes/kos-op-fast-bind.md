# KOS_OP_FAST_BIND
- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[..., ref] → [...]`. **BIND**: use this reference to resolve the
  given slot (name). See §1.9. Not interchangeable with FAST_STORE.
- **Invariants**: Slot index in range.
- **Compiler form:** Typically post-compile (from VAR_BIND lowering) or compiler emits with slot index.

**Example:** Bind slot 0 to the reference at TOS.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., ref] |
| 1     | FAST_BIND| 0   | 0 (a)    | BIND: slot 0 resolves to this ref; stack: [...] |

