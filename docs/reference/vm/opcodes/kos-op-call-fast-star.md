# KOS_OP_CALL_FAST_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot index), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same as CALL_VAR_STAR. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

