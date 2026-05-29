# KOS_OP_CHECK_NOT_IN

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., container, value]` (value = TOS). Pops both; tests `value not in container`. If **true** (i.e. value not in container), jump to `arg`; else fall through. Stack after: `[...]` in both cases.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when value not in container (e.g. guard for "in" branch).

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (s)    | stack: [..., container] |
| 1     | CONST      | 1   | 1 (key) | stack: [..., container, value] |
| 2     | CHECK_NOT_IN | 5 | 5 (-> 5)| if value not in container → pc=5; else stack: [...] |
| 3     | RETURN     | 1   | 1        | |
| 5     | LABEL      | —   | —        | miss |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
