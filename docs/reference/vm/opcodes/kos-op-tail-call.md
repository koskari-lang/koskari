# KOS_OP_TAIL_CALL
- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: Same as CALL: `[..., callable, arg_1, ..., arg_n]`. The arguments are taken off the stack before the callee is examined. A Koskari callable takes over the current frame; any other callable is called after the current frame is dropped, when it has a parent, and its result goes to the caller. Control then continues in the frame now on top, running any wind thunks on the way.
- **Execution model**: When the callable is a KosExpression or KosFunction, the VM replaces the current frame (tail install). For all other callable types, the opcode drops the current frame when parent exists, calls the callable, and pushes the result.
- **Invariants**: Stack has at least 1 + arg elements. KosExpression allows no arguments (arg must be 0); otherwise TypeError.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL in function-body code; never emitted by a special.

**Example:** Tail call with 0 args (replace frame with callee). Pool: 0 = fn.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | TAIL_CALL| 0   | 0        | replace frame; run callee. stack: [...,] (new frame) |

