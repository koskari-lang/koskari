# KOS_OP_VAR_FORMAT
- **Style**: VAR. Arg = constant index of a name.
- **Status**: Implemented.
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT (arg=1): resolve the name in the current frame's namespace and push `PyObject_Format(obj, NULL)`.
- **Compiler form:** Produced by `Compiler.fuse_check_feeders` when OBJECT_FORMAT (arg=1) is fed by a bare VAR load. Lowers to FAST_FORMAT through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT.c`
