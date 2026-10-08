# KOS_OP_CALL_VAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}] → [..., result]`. The callee is read from the frame namespace by name and is not on the stack. A Koskari callable runs in the evaluator with the arguments taken from the stack; any other callable is called with them and its result pushed, awaited when awaitable.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

