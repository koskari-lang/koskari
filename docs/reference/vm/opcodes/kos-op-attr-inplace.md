# KOS_OP_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG). Row a0 = operator selector and count, a1 = the attribute name.
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj.attr in place with the count values left to right, store it back, drop the object and the values; pushes nothing (the special appends CONST None). Cold: the operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is an expression.
- **Implementation**: `koskari/lib/opcode/ATTR_INPLACE.c`
