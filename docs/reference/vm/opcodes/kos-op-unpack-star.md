# KOS_OP_UNPACK_STAR
- **Style**: EXTRA (`COUNT`, `COUNT`, `NOARG`, `NOARG`). Instruction arg =
  index into the code object's **extra** table; that row has `a0 = left`,
  `a1 = right` (both non-negative counts).
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK_STAR.c`)
- **Semantics**: Pop TOS, `PySequence_List(raw)`. Require `left + right > 0`
  and `len(seq) >= left + right`. Let `L = len(seq)`. The **middle** slice is
  `seq[left : L - right]` (inclusive start, exclusive end), materialized as a
  **tuple** (new reference). Pushes are ordered so that after all pushes,
  **TOS is the first prefix element** `seq[0]` (when `left > 0`):
  1. **Suffix** scalars: indices `L-1, L-2, ..., L-right` (each via
     `KosStackBlock_PushRef`).
  2. **Middle**: one value — the tuple — via `KosStackBlock_PUSH` (**stolen**
     reference from `PySequence_Tuple`).
  3. **Prefix** scalars: indices `left-1, ..., 0` (each via `PushRef`).
- **Stack (TOS right)**: `[..., raw] → [..., seq[L-1], ..., seq[L-right],
  middle, seq[left-1], ..., seq[0]]` when all parts are non-empty; adjust when
  `left` or `right` is zero (no prefix or suffix pushes for that side).
- **Invariants**: Extra row matches schema; `left + right` fits in uint16;
  realized value must be a valid sequence for `PySequence_List` / slicing.
- **Errors**: `ValueError` if `left + right == 0` or `len(seq) < left + right`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple
  `(left, right, 0, 0)`; `NOARG` slots are ignored by the assembler. Same
  pattern as other `EXTRA` opcodes (see §1.7).

**Example:** `left=2`, `right=1`, seq `(1,2,3,4)`: suffix `[4]`, middle
`(3,)`, prefix `2, 1` with TOS `1`.

| index | opcode       | arg | arg view   | comment |
|------:|--------------|-----|------------|---------|
| 0     | CONST        | 0   | 0 ((1,2,3,4)) | |
| 1     | UNPACK_STAR  | 0   | 0 (2, 1, …) | extra row: left=2, right=1 |
| 2     | RETURN       | 4   | 4          | stack bottom..top: 4, (3,), 2, 1 |

