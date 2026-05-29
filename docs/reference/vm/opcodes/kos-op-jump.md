# KOS_OP_JUMP

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: No change.
- **Invariants**: Target must be inside current extent or at its `pop_pc` (jump locality).
- **Compiler form:** Pre-compile arg = string label name (must match a LABEL in
  the same compilation unit). Post-compile arg = pc of that LABEL.

**Pre-compile example:** Same snippet as LABEL; JUMP uses the string `"done"`.

```
["JUMP", "done"],
["CONST", 99],
["LABEL", "done"],
["RETURN", 1],
```

**Post-compile example:** After finalize, LABEL is stripped (see LABEL). JUMP's target was the LABEL; it becomes the next instruction (index 1). Constant pool: 0 = 99.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | JUMP   | 1   | 1 (-> 1) | pc := 1 (target "done" after LABEL stripped) |
| 1     | CONST  | 0   | 0 (99)   | dead code |
| 2     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
