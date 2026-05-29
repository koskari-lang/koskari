# KOS_OP_VAR_STORE_CONST

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). **STORE**: store the constant value into the given name via `frame_ns_store`. Same semantics as **VAR_STORE**, but the value is taken from the constant pool (extra row field `a1`), not from TOS.
- **Peephole**: **`Compiler.compress`** fuses **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` into this opcode with pre-compile arg **`(name, value, None, None)`**; the **`CONST`** is removed from the stream. See §1.8.
- **Lowering**: **`Compiler.convert_fast`** (when the name has a fixed fast slot in FUNCTION mode) may replace **`VAR_STORE_CONST`** with **`FAST_STORE_CONST`**, rewriting the VAR field to a slot index and preserving the constant operand.
- **Invariants**: Name must already be bound. Name and value are internalized into the extra row at finalize.
- **Compiler form:** Pre-compile arg = 4-tuple `(name, value, None, None)`; post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/VAR_STORE_CONST.c`
- **Covered by**: `tests/compile/test_compress.py`, `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 42 into name `"x"` without pushing 42. Pre-compile stream after compress: one **`VAR_STORE_CONST`**; pool/extra filled at finalize. Post-compile arg view uses extra index **0** (illustrative).

| index | opcode          | arg | arg view              | comment |
|------:|-----------------|-----|-----------------------|---------|
| 0     | VAR_STORE_CONST | 0   | 0 → ('x', 42, …)      | STORE 42 into "x"; stack unchanged |
| 1     | RETURN          | 1   | 1                     | |

**Example (fusion source):** `(compile/setq x 42)` emits value, **`VAR_STORE`**, **`CONST`** `None`; compress turns **`CONST`** `42` + **`VAR_STORE`** `x` into **`VAR_STORE_CONST`**.

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
