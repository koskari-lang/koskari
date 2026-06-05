# KOS_OP_POP_SHIFT
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops TOS as result; builds a transfer plan that unwinds from the current position through the matching SHIFT and PROMPT nodes (TRUNCATE_AND_INJECT step drops both and their stack block), then injects the single result value onto the outer frame's stack (below the PROMPT's block boundary). Handler returns without invoking k.
- **Invariants**: Must be inside a PUSH_SHIFT extent (parent is PROMPT extent). Stack block owned by PROMPT is dropped as part of the TRUNCATE_AND_INJECT step; the injected value lands on the frame's stack below the former block boundary.
- **Compiler form:** No argument.

**Example:** Shift handler returns value without invoking k.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 4     | POP_SHIFT| 0   | 0        | pop result; unwind through SHIFT+PROMPT; drop stack block; inject result onto outer stack |

