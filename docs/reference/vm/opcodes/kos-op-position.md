# KOS_OP_POSITION
- **Style**: COMPILE. Compile-time only.
- **Status**: Compile-time; **stripped** in post-compile under normal circumstances. If retained, no-op at runtime.
- **Stack**: No change.
- **Invariants**: Position is recorded in the code object's position table during finalize. The POSITION instruction itself is normally omitted from the bytecode.
- **Behavior**: Source position marker. Finalize records the position (line, col) in the position table and **strips** the POSITION instruction. If a POSITION is retained, it advances PC only (safety net).
- **Compiler form:** Pre-compile arg may be (line, col) or a position token; finalize records position in the position table and strips POSITION. Post-compile: **normally omitted**; if retained, arg typically 0 or unused.

**Example:** Normally POSITION is stripped; position is stored in the position table only, so the bytecode would be: 0 CONST, 1 RETURN. If POSITION were retained (safety net), it would look like:

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | POSITION| 0   | —        | line 1, col 0 (in position table); no-op if executed |
| 1     | CONST   | 0   | 0 (1)    | stack: [..., 1] |
| 2     | RETURN  | 1   | 1        | |


## 4. Stack

