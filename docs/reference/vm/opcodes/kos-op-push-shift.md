# KOS_OP_PUSH_SHIFT
- **Style**: COUNT. Arg = `0` for the default prompt tag, or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Captures the continuation: one walk from the top of the dynamic chain locates the nearest matching prompt while offering handle(SHIFT) to each node crossed (a generator boundary denies). The captured continuation is the delimited region with the shift expression as its hole -- a cloned chain segment, so a shift hosted in a callee captures through the calling frames up to the prompt. The shift then aborts to that prompt: every node above it is left (a dyn-wind after thunk and a `dyn-with` exit run; a `try` and a plain `with` are dropped without running their `finally` body or exit), a SHIFT marker is attached to the prompt, and the captured continuation is delivered to it at the shift block's first instruction. A shift hosted in a callee leaves that callee's frame with the region, and a copy of the frame is attached to the SHIFT for the shift block to run in. SHIFT owns the segment the shift body's operands go on; the region's pending operands are in the continuation.
- **Pushes:** KOS_DYN_SHIFT (bookmark for the shift body: owns its operand segment, no handlers).
- **Invariants**: Extent KOS_EXT_SHIFT pairs with POP_SHIFT. The SHIFT's parent is the prompt the shift aborted to. Invoking the captured continuation re-delimits: each invocation installs a copy of the region under a fresh prompt instance, so a shift inside that run terminates only that invocation.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a node that denies handle(SHIFT) (a generator boundary) raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`.
- **Compiler form:** The arg is the count, `0` or `1`, before and after compilation.

**Example:** shift k: body. Capture k, run shift body with k on stack. The count `0` is the default tag.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0        | capture k; abort to the prompt; attach SHIFT marker; enter shift block with k on stack |

