# KOS_OP_PUSH_SHIFT
- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: Captures the continuation: one walk from the top of the dynamic chain locates the nearest matching prompt while offering handle(SHIFT) to each node crossed (a generator boundary denies). The captured continuation is the delimited region with the shift expression as its hole -- a cloned chain segment, so a shift hosted in a callee captures through the calling frames up to the prompt. Pushes a SHIFT marker recording the captured prompt's instance, pushes the captured continuation on stack, and enters the shift block in place; no unwinding happens at capture. SHIFT does not push its own stack block.
- **Pushes:** KOS_DYN_SHIFT (simple marker; no stack block, no handlers).
- **Invariants**: Extent KOS_EXT_SHIFT pairs with POP_SHIFT. Stack block is owned by PROMPT, not SHIFT. Invoking the captured continuation re-delimits: each invocation runs the region under a fresh prompt instance, so a shift inside that run terminates only that invocation.
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k, run shift body with k on stack. Pool: 0 = prompt_id.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (pid)  | capture k in place; push SHIFT marker; enter shift block with k on stack |

