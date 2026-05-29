# KOS_OP_POP_NS
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Restore frame namespace from saved_ns; pop NS node. No stack block.

**Example:** Pool: N/A.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | POP_NS | 0   | 0        | pop KosDynNs; restore saved namespace |

- **Invariants**: Top of chain must be KOS_DYN_NS.

---

## 19. Object protocol

Object protocol opcodes: single Python operation on TOS (and optionally below); pop operands, push result. All **Implemented** unless noted.

- **Comparisons**: OBJECT_EQ, OBJECT_NE, OBJECT_LT, OBJECT_LE, OBJECT_GT, OBJECT_GE (COUNT) — stack `[..., a1, a2, ... aN] → [..., result]`, `N >= 2`.
- **Predicates**: OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS, OBJECT_IS_NOT; OBJECT_IS_INSTANCE (COUNT), OBJECT_IS_SUBCLASS (COUNT).
- **Numeric**: OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY, OBJECT_POWER, OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_MODULO, OBJECT_* (bit ops, matrix multiply).
- **General**: OBJECT_ITER, OBJECT_AITER, OBJECT_LEN, OBJECT_NOT, OBJECT_REPR, OBJECT_STR, OBJECT_FORMAT, OBJECT_FORMAT_CONST, OBJECT_TYPE, OBJECT_DIR, OBJECT_VARS, OBJECT_TRUTH, OBJECT_DIVMOD, OBJECT_BIT_INVERT, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT.

Each `OBJECT_*` opcode in `opcodes.incl` has its own subsection below.

