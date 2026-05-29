# KOS_OP_PUSH_SHIFT

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: Captures continuation from POP_SHIFT+1 to POP_PROMPT; builds an unwind transfer plan to unwind from the current position to the matching prompt (exclusive); runs the plan; pushes the captured continuation onto the prompt's stack (which is now the active stack after PUSH_PROMPT's block push); enters the shift block. Shift body runs with k on stack. SHIFT does not push its own stack block; the PROMPT's block is the active block throughout.
- **Pushes:** KOS_DYN_SHIFT (simple marker; no stack block, no handlers).
- **Invariants**: Capture validated by kos_validate_capture (prompt reachability in chain). Extent KOS_EXT_SHIFT pairs with POP_SHIFT. Stack block is owned by PROMPT, not SHIFT.
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k, run shift body with k on stack. Pool: 0 = prompt_id.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (pid)  | capture k; unwind to prompt; push k (onto prompt's stack); enter shift block |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
