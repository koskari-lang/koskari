# KOS_OP_VAR_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG). Row a0 = operator selector and count, a1 = the object name, a2 = the attribute name.
- **Status**: Implemented. Cache: one KOS_CACHE_FIELD slot; a single resolved storage location serves both the read of obj.attr and the write-back.
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Resolve the object by name, reduce obj.attr in place with the count values left to right, store it back; pushes nothing (the special appends CONST None). Cold: the operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is a symbol. May lower to **FAST_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_INPLACE.c`
