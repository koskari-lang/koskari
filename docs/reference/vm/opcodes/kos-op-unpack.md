# KOS_OP_UNPACK
- **Style**: COUNT. Arg = `n` (number of values to push).
- **Stack**: `[..., raw] → [..., v_{n-1}, ..., v_1, v_0]` where `v_i` is the
  `i`-th element of the realized sequence and **TOS = v_0 = seq[0]** (so the
  first binding in source order can use `VAR_STORE` / `FAST_STORE` on TOS
  first). Implementation: pop TOS, `PySequence_List(raw)` (new list), require
  `len(seq) == n`, then `KosStack_PushRef` for indices `n-1` down to `0`,
  then `Py_DECREF(seq)`.
- **Invariants**: `n >= 0`. For `n == 0`, TOS must be an empty sequence after
  realization.
- **Errors**: `ValueError` if length ≠ `n`; `TypeError` or other error if
  `PySequence_List` fails.
- **Compiler form:** Pre-compile / post-compile arg = `n` (uint16).

**Example:** Unpack `(1, 2, 3)` so TOS is `1`, then return three values (stack
bottom to top matches push order).

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ((1,2,3)) | stack: [..., (1,2,3)] |
| 1     | UNPACK  | 3   | 3        | stack: [..., 3, 2, 1]; TOS = 1 |
| 2     | RETURN  | 3   | 3        | |

