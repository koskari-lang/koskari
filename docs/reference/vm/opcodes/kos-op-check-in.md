# KOS_OP_CHECK_IN
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., container, value]` (value = TOS). Pops both; tests `value in container` (PySequence_Contains). If **false** (check fails), jump to `arg`; else fall through. Stack after: `[...]` in both cases (both operands always popped).
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label (e.g. "miss"); post-compile arg = pc.

**Example: value in container (guard / pattern match).** Constant pool: 0 = `[1, 2, 3]`, 1 = `2`, 2 = `1`, 3 = `0`. If `2 in [1,2,3]` push 1 and return; else jump to "miss", push 0 and return. Covered by `tests/lib/test_ceval.py`: `test_check_in_continues_when_contained`, `test_check_in_jumps_when_not_contained`.

| index | opcode   | arg | arg view   | comment |
|------:|----------|-----|------------|---------|
| 0     | CONST    | 0   | 0 ([1, 2, 3]) | stack: [..., container] |
| 1     | CONST    | 1   | 1 (2)      | stack: [..., container, value] |
| 2     | CHECK_IN | 5   | 5 (-> 5)   | if value not in container → pc=5; else fall through. stack: [...] |
| 3     | CONST    | 2   | 2 (1)      | stack: [..., 1] |
| 4     | RETURN   | 1   | 1          | |
| 5     | LABEL    | —   | —          | miss |
| 6     | CONST    | 3   | 3 (0)      | stack: [..., 0] |
| 7     | RETURN   | 1   | 1          | |

