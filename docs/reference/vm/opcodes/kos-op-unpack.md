# KOS_OP_UNPACK
- **Style**: COUNT. Arg = `n` (number of values to push).
- **Stack**: `[..., raw] → [..., v_{n-1}, ..., v_1, v_0]` where `v_i` is the
  `i`-th element of the realized sequence and **TOS = v_0 = seq[0]** (so the
  first binding in source order can use `VAR_STORE` / `FAST_STORE` on TOS
  first). A tuple or list is read in place; any other iterable is pulled for
  `n` items plus one, so an over-long iterable is refused at its extra item
  and never drained.
- **Invariants**: `n >= 0`. For `n == 0`, TOS must be an empty iterable.
- **Errors**: `ValueError` if the count is not `n`; `TypeError` when TOS is
  not iterable.
- **Compiler form:** Pre-compile / post-compile arg = `n` (uint16).

**Example:** Unpack `(1, 2, 3)` so TOS is `1`, then return three values (stack
bottom to top matches push order).

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ((1,2,3)) | stack: [..., (1,2,3)] |
| 1     | UNPACK  | 3   | 3        | stack: [..., 3, 2, 1]; TOS = 1 |
| 2     | RETURN  | 3   | 3        | |

