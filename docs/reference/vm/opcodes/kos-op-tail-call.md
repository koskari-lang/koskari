# KOS_OP_TAIL_CALL
- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: Same as CALL: `[..., callable, arg_1, ..., arg_n]`. Extracts args via PopTailPtr (small counts) or PopTail (large) before branching; opcode owns storage, DECREFs at end. For KosCallable: tail install recycles the frame. For Plain Python: `kos_dyn_pop_frame` when parent exists, then PyObject_Vectorcall, kos_push_or_await. Returns **`EVAL_CONTROL_RETURNED()`** when continuing (triggers **`kos_dynnode_on_control_returned`** for wind thunks).
- **Execution model**: When the callable is a KosExpression or KosFunction, the VM replaces the current frame (tail install). For all other callable types, the opcode drops the current frame when parent exists, calls the callable, and pushes the result.
- **Invariants**: Stack has at least 1 + arg elements. KosExpression allows no arguments (arg must be 0); otherwise TypeError.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL in function-body code; never emitted by a special.

**Example:** Tail call with 0 args (replace frame with callee). Pool: 0 = fn.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | TAIL_CALL| 0   | 0        | replace frame; run callee. stack: [...,] (new frame) |

