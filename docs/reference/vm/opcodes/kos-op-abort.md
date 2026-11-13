# KOS_OP_ABORT
- **Style**: EXTRA (`COUNT`, `LABEL`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = the instruction after the handler, which nothing at run time reads. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: The handler is the instructions that follow ABORT, up to a RETURN. One walk from the top of the dynamic chain locates the nearest matching prompt (a generator boundary denies). ABORT then aborts to that prompt: every node above it is left (a dyn-wind after thunk and a `dyn-with` exit run; a `try` and a plain `with` are dropped without running their `finally` body or exit). When an after thunk or exit raises, the abort is abandoned and the raise propagates from where it ran, with the region's operands still in place. Once the prompt is at the top of the chain, a frame is attached above it that runs the same code object over the namespace live at the ABORT, standing at the instruction after ABORT with an empty stack. The handler's return ends the prompt and is the RESET's value, as the body's return would have been. Nothing is captured.
- **Pushes:** The handler's KOS_DYN_FRAME above the prompt, once the abort has left the region, so an ABORT or a shift in the handler finds the same prompt. The frame holds the code and the namespace itself, since the frame the ABORT ran in is gone by then.
- **Invariants**: The handler shares its names with the function the ABORT is in.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a generator boundary on the way raises `KoskariPromptUnreachable` with the boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`, `abort`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, label, 0, 0)`; `NOARG` slots are ignored by the assembler. The compiler emits ABORT, the handler body, `RETURN 1`, then the label. `abort` emits count `0`; `abort-at` compiles its tag first and emits count `1`.

**Example:** `(reset (+ 1 (abort 5)))` as a function's whole body. The ABORT's extra row: `(0, -> 4, 0, 0)`.

| index | opcode         | arg | arg view              | comment |
|------:|----------------|-----|-----------------------|---------|
| 0     | RESET          | 0   | 0 (0, -> 6)           | push prompt; run 1 onward in a frame above it |
| 1     | CONST          | 0   | 0 (1)                 | region operand; stack: [1] |
| 2     | ABORT          | 1   | 1 (0, -> 4)           | abort to the prompt; run 3 onward above it |
| 3     | RETURN_CONST   | 1   | 1 (5)                 | the handler's value; ends the prompt |
| 4     | OBJECT_OPERAND | 2   | ADD x2                | never reached |
| 5     | RETURN         | 1   | 1                     | |
| 6     | RETURN         | 1   | 1                     | the function's value, the reset's |
