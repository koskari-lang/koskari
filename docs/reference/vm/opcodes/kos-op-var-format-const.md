# KOS_OP_VAR_FORMAT_CONST
- **Style**: EXTRA (VAR, CONST). Row = obj name (pool), rules string (pool).
- **Status**: Implemented.
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT_CONST: resolve the name in the current frame's namespace and push `PyObject_Format(obj, rules)`.
- **Compiler form:** Produced by `Compiler.fuse_check_feeders` when OBJECT_FORMAT_CONST is fed by a bare VAR load. Lowers to FAST_FORMAT_CONST through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT_CONST.c`
