# KOS_OP_PUSH_PROMPT
- **Style**: COUNT. Arg = `0` for the default prompt tag, or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: The prompt owns a fresh operand segment (so the reset body has an isolated stack), resolves the PROMPT extent at the push site and records the region's closing pc on the node (a shift's abort resumes the frame at that POP_PROMPT), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT). PROMPT owns the segment for the delimited region.
- **Invariants**: SHIFT captures up to this prompt. An exception unwinding past a PROMPT drops the segment and unlinks the node in one step.
- **Compiler form:** The arg is the count, `0` or `1`, before and after compilation.

**Example:** reset (push prompt). The count `0` is the default tag.

| index | opcode      | arg | arg view     | comment |
|------:|-------------|-----|--------------|---------|
| 0     | PUSH_PROMPT | 0   | 0            | push prompt (owning its segment) onto chain |
| 1     | …           | …   | …            | body before shift (continuation prefix) |
| 2     | SHIFT       | 0   | 0 (0, fspec) | capture k; abort to the prompt; call the handler with k |
| 3     | …           | …   | …            | continuation (body after shift) |
| 4     | POP_PROMPT  | 0   | 0            | drop segment; pop prompt |
