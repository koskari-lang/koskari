# KOS_OP_BREAK
- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Stack**: Pops `arg` elements; control transfers to break target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent break_pc (instruction after the matching POP_LOOP or POP_FOREACH).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | BREAK  | 0   | 0        | pop 0, jump to break target (e.g. pc 6) |
| …     | …      | …   | …        | |
| 6     | POP_LOOP | 0 | 0        | break target (LOOP extent; use POP_FOREACH for FOREACH) |

