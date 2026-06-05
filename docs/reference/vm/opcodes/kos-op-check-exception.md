# KOS_OP_CHECK_EXCEPTION
- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Pop TOS (exception type or tuple of types). If current exception matches that value, leave PC unchanged; else set PC to arg (branch to next handler).
- **Invariants**: Jump locality for target. Used inside exception handling path.
- **Compiler form:** Pre-compile arg = string label (next handler or re-raise); post-compile arg = pc.

**Example:** In except block: type(s) on stack; pop and match; jump if no match.

| index | opcode          | arg | arg view | comment |
|------:|-----------------|-----|----------|---------|
| 3     | CONST           | 0   | 0 (typ)  | stack: [..., typ] |
| 4     | CHECK_EXCEPTION | 7   | 7 (-> 7) | pop typ; if exc not match → pc=7 |
| 5     | POP             | 1   | 1        | consume exc |
| 6     | RETURN          | 1   | 1        | handle |
| 7     | LABEL           | —   | —        | re-raise or next handler |


## 9.1 Switch dispatch

Switch dispatch opcodes implement multi-way **equality** branching. A typical
**checks** region runs comparisons in order: each comparison **falls through**
when it does not match so the next check can run without re-pushing the
scrutinee. A **match** jumps into a shared **body** (or the same body for
several keys).

**`KOS_OP_SWITCH_EQ`** compares two stack operands, discriminant **`left`**
below key **`right`** (TOS). On **match**, both are popped and the PC is set
to **`arg`** (the body). On **mismatch**, only **`right`** is popped; **`left`**
remains so the following code can push another key and use another
**`SWITCH_EQ`**.

The **single-slot** variants (**`SWITCH_EQ_CONST`**, **`SWITCH_EQ_VAR`**, …)
compare the scrutinee **`s`** at TOS to an RHS from the extra row. On
**match**, **`s`** is popped and the PC jumps to **`a0`**. On **mismatch**,
the stack is **unchanged** and execution falls through.

See archive/switch-eq-opcodes.md for family rationale and the
surface `compile/switch` form. The full SWITCH_EQ family is implemented.

