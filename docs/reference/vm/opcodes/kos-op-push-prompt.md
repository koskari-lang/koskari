# KOS_OP_PUSH_PROMPT
- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: The prompt owns a fresh operand segment (so the reset body has an isolated stack), resolves the PROMPT extent at the push site and records the region's closing pc on the node (POP_SHIFT resumes at that pc + 1), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; PUSH_SHIFT captures up to this prompt). PROMPT owns the segment for the delimited region; SHIFT does not create one.
- **Invariants**: PUSH_SHIFT captures up to this prompt. Exception unwinding past a PROMPT drops the segment and unlinks the node via the HANDLES_EXCEPTIONS capability (no kind-specific check in the unwind loop).
- **Compiler form:** Pre-compile arg may be prompt id value or symbol; post-compile arg = pool index.

**Example:** reset (push prompt). Pool: 0 = prompt_id.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0 (pid)  | push prompt (owning its segment) onto chain |
| 1     | …          | …   | …        | body before shift (continuation prefix) |
| 2     | PUSH_SHIFT | 0   | 0 (pid)  | capture k; enter shift block with k on stack |
| 3     | …          | …   | …        | shift body (k available) |
| 4     | POP_SHIFT  | 0   | 0        | return from shift without invoking |
| 5     | …          | …   | …        | continuation (body after shift) |
| 6     | POP_PROMPT | 0   | 0        | drop segment; pop prompt |

