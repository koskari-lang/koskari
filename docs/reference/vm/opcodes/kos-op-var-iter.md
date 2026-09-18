# KOS_OP_VAR_ITER
- **Style**: VAR. Arg = constant index of a name.
- **Stack**: `[...] -> [..., iterator]`. Fused VAR + OBJECT_ITER: resolve the name in the current frame's namespace and push its iterator, acquired uniformly over both protocols (`kos_iter_acquire`).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_ITER is fed by a bare VAR load. Lowers to FAST_ITER through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_ITER.c`
