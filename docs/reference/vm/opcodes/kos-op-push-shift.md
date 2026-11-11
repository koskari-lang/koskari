# KOS_OP_PUSH_SHIFT
- **Style**: CONST. Arg = tag selector (constant pool index, post-compile): `0` for the default prompt tag, `None` for a tag the instruction pops from the stack. Any other constant is refused. Tags match by identity.
- **Stack**: Captures the continuation: one walk from the top of the dynamic chain locates the nearest matching prompt while offering handle(SHIFT) to each node crossed (a generator boundary denies). The captured continuation is the delimited region with the shift expression as its hole -- a cloned chain segment, so a shift hosted in a callee captures through the calling frames up to the prompt. Pushes a SHIFT marker recording the captured prompt's instance, pushes the captured continuation on stack, and enters the shift block in place; no unwinding happens at capture. SHIFT owns the segment the shift body's operands go on; the PROMPT's segment keeps the region's pending operands for the continuation to resume over.
- **Pushes:** KOS_DYN_SHIFT (bookmark for the shift body: owns its operand segment, no handlers).
- **Invariants**: Extent KOS_EXT_SHIFT pairs with POP_SHIFT. Stack block is owned by PROMPT, not SHIFT. Invoking the captured continuation re-delimits: each invocation runs the region under a fresh prompt instance, so a shift inside that run terminates only that invocation.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a node that denies handle(SHIFT) (a generator boundary) raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`.
- **Compiler form:** Pre-compile arg is the selector value (`0` or `None`); post-compile arg = pool index.

**Example:** shift k: body. Capture k, run shift body with k on stack. Pool: 0 = the default tag selector.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (tag)  | capture k in place; push SHIFT marker; enter shift block with k on stack |

