# KOS_OP_SHIFT_FAST
- **Style**: EXTRA (`COUNT`, `LABEL`, `FASTVAR`, `NOARG`). As [SHIFT_VAR](kos-op-shift-var.md), with `a2` = the fast slot the continuation is bound to.
- **Stack**: As SHIFT_VAR: captures the continuation up to the prompt, stores it into the fast slot, aborts to the prompt, and runs the handler that follows in a frame above it.
- **Invariants**: As SHIFT_VAR. The slot must be a fast slot of the current frame namespace.
- **Errors**: As SHIFT_VAR.
- **Compiler form:** Lowered from `SHIFT_VAR` when the continuation's name has a fast slot.

**Example:** `(reset (+ 1 (shift k (k 5))))` as a function's whole body, with `k` its fast variable 0. The SHIFT_FAST's extra row: `(0, -> 6, 0, 0)`.

| index | opcode         | arg | arg view                      | comment |
|------:|----------------|-----|-------------------------------|---------|
| 0     | RESET          | 0   | 0 (0, -> 8)                   | push prompt; run 1 onward in a frame above it |
| 1     | CONST          | 0   | 0 (1)                         | region operand; stack: [1] |
| 2     | SHIFT_FAST     | 1   | 1 (0, -> 6, fast 0 ("k"))     | capture and bind k; abort to the prompt; run 3 onward above it |
| 3     | CONST          | 1   | 1 (5)                         | |
| 4     | TAIL_CALL_FAST | 2   | fast 0 ("k"), 1               | (k 5) |
| 5     | RETURN         | 1   | 1                             | the handler's value; ends the prompt |
| 6     | OBJECT_OPERAND | 2   | ADD x2                        | the continuation resumes here with the hole's value |
| 7     | RETURN         | 1   | 1                             | the body's value, or a copy's, ends its prompt |
| 8     | RETURN         | 1   | 1                             | the function's value, the reset's |
