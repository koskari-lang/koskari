# KOS_OP_FAST_STORE_CONST

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). **STORE**: store the constant value into the given fast slot via `KosNamespace_FastStore`. Same semantics as **FAST_STORE**, but the value is from the constant pool (extra row `a1`), not TOS.
- **Peephole**: Not fused directly from **`CONST`** + **`FAST_STORE`** in **`compress`** today; typically appears when **`VAR_STORE_CONST`** is lowered by **`convert_fast`** for a local or closure name.
- **Invariants**: Slot must already be bound; slot index in range; frame namespace must support fast locals (`KosNamespace_SupportsFast`).
- **Compiler form:** Pre-compile arg = 4-tuple `(slot, value, None, None)` (slot may be integer after **`convert_fast`**); post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/FAST_STORE_CONST.c`
- **Covered by**: `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 100 in slot 0 without pushing 100.

| index | opcode           | arg | arg view           | comment |
|------:|------------------|-----|--------------------|---------|
| 0     | FAST_STORE_CONST | 0   | 0 → (0, 100, …)    | STORE 100 in slot 0; stack unchanged |
| 1     | FAST             | 0   | 0 (a)              | stack: [..., 100] |
| 2     | RETURN           | 1   | 1                  | |

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
