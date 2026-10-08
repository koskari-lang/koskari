# KOS_OP_CALL_VAR_STAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), STAR (bitflags: 1 = *args, 2 = **kwargs), NOARG, NOARG.
- **Stack**: Same as CALL_STAR minus callable. `[..., pos, kwd]` or subsets per flags. The callee is read from the frame namespace by name. The operands present are popped per the flags, `pos` is made a tuple, and the callee is called with them.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

