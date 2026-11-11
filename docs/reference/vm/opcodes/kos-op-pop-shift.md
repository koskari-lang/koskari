# KOS_OP_POP_SHIFT
- **Style**: NOARG.
- **Stack**: Pops TOS as result. The delimiter is the SHIFT's parent, the prompt the shift aborted to. For a live reset: builds a transfer plan that unwinds through the SHIFT and PROMPT (dropping both and the PROMPT's segment), injects the result onto the outer frame's stack, and resumes at the closing pc the prompt recorded at push, + 1. For the prompt of an applied continuation (its parent is the copy's frame, sitting on a DELIM; no POP_PROMPT pairs with it as its own reset): the result is the value of that k-invocation, delivered at the invoke site through that DELIM.
- **Invariants**: Must be inside a PUSH_SHIFT extent. The prompt carries the resume pc, so no extent lookup happens at pop time.
- **Compiler form:** No argument.

**Example:** Shift handler returns value without invoking k.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 4     | POP_SHIFT| 0   | 0        | pop result; unwind through SHIFT+PROMPT; drop segment; inject result onto outer stack |

