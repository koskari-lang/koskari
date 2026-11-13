# KOS_OP_RESET
- **Style**: EXTRA (`COUNT`, `LABEL`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = the instruction where the frame goes on after the body. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Pushes a prompt carrying the tag, sets the frame's resume position to `a1`, and pushes a frame above the prompt that runs the instructions following RESET. That frame runs the same code object over the same namespace as the frame RESET ran in, so the body reads and binds the enclosing function's names. The body ends in a RETURN. The body's return, or the return of a shift's handler called above the same prompt, unlinks the prompt and pushes the value on the stack of the frame below, as a call's return does; that frame goes on at `a1`.
- **Pushes:** KOS_DYN_PROMPT, then the body's KOS_DYN_FRAME above it. The prompt owns no operand segment: the region's operands are in the frames above it.
- **Invariants**: A shift captures up to this prompt, and an ABORT aborts to it. A prompt at the root of a context ends the context when the frame on it returns. An exception unwinding past a PROMPT unlinks it.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, label, 0, 0)`; `NOARG` slots are ignored by the assembler. The compiler emits RESET, the body, `RETURN 1`, then the label. `reset` emits count `0`; `reset-at` compiles its tag first and emits count `1`.

**Example:** `(+ 1 (reset (* x 2)))` as the body of a function of `x`. The extra row: `(0, -> 4, 0, 0)`.

| index | opcode             | arg | arg view                  | comment |
|------:|--------------------|-----|---------------------------|---------|
| 0     | CONST              | 0   | 0 (1)                     | stack: [1] |
| 1     | RESET              | 0   | 0 (0, -> 4)               | push prompt; run 2 onward in a frame above it |
| 2     | FAST_OPERAND_CONST | 1   | MULTIPLY, fast 0 ("x"), 2 | body, reading the function's `x` |
| 3     | RETURN             | 1   | 1                         | the body's value; ends the prompt |
| 4     | OBJECT_OPERAND     | 2   | ADD x2                    | the function goes on with the reset's value |
| 5     | RETURN             | 1   | 1                         | |
