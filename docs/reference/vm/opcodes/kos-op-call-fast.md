# KOS_OP_CALL_FAST
- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_VAR. Callable is loaded from fast local slot via `KosNamespace_FastDeref`; not on stack.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

