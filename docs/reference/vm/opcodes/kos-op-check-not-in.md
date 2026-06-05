# KOS_OP_CHECK_NOT_IN
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., item, container]` (container = TOS). Pops both; tests `item not in container`. If **true** (i.e. item not in container), jump to `arg`; else fall through. Stack after: `[...]` in both cases.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when item not in container (e.g. guard for "in" branch).

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (key)  | stack: [..., item] |
| 1     | CONST      | 1   | 1 (s)    | stack: [..., item, container] |
| 2     | CHECK_NOT_IN | 5 | 5 (-> 5)| if item not in container → pc=5; else stack: [...] |
| 3     | RETURN     | 1   | 1        | |
| 5     | LABEL      | —   | —        | miss |
