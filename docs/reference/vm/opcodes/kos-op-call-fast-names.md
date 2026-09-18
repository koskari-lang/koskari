# KOS_OP_CALL_FAST_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot index), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: Same as CALL_VAR_NAMES. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

