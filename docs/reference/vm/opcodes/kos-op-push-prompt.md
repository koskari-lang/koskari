# KOS_OP_PUSH_PROMPT
- **Style**: COUNT. Arg = `0` for the default prompt tag, or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: The prompt owns a fresh operand segment (so the reset body has an isolated stack), resolves the PROMPT extent at the push site and records the region's closing pc on the node (POP_SHIFT resumes at that pc + 1), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; PUSH_SHIFT captures up to this prompt). PROMPT owns the segment for the delimited region; SHIFT does not create one.
- **Invariants**: PUSH_SHIFT captures up to this prompt. An exception unwinding past a PROMPT drops the segment and unlinks the node in one step.
- **Compiler form:** The arg is the count, `0` or `1`, before and after compilation.

**Example:** reset (push prompt). The count `0` is the default tag.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0        | push prompt (owning its segment) onto chain |
| 1     | …          | …   | …        | body before shift (continuation prefix) |
| 2     | PUSH_SHIFT | 0   | 0        | capture k; enter shift block with k on stack |
| 3     | …          | …   | …        | shift body (k available) |
| 4     | POP_SHIFT  | 0   | 0        | return from shift without invoking |
| 5     | …          | …   | …        | continuation (body after shift) |
| 6     | POP_PROMPT | 0   | 0        | drop segment; pop prompt |

