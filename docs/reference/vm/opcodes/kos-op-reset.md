# KOS_OP_RESET
- **Style**: EXTRA (`COUNT`, `ASIDE`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = the aside that is the reset's body. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Pops the tag when the count is `1`. Pushes a prompt carrying the tag, then a frame above the prompt that runs the aside (see [PUSH_ASIDE](kos-op-push-aside.md)). The body's return, or the return of a handler run above the same prompt, unlinks the prompt and pushes the value on the stack of the frame RESET ran in, as a call's return does; that frame goes on at the next instruction.
- **Pushes:** KOS_DYN_PROMPT, then the body's KOS_DYN_FRAME above it. The prompt owns no operand segment: the region's operands are in the frames above it.
- **Invariants**: A shift captures up to this prompt, and an ABORT aborts to it. A prompt at the root of a context ends the context when the frame on it returns. An exception unwinding past a PROMPT unlinks it. The compiler declares a reset's body with `None` for its spec, so the body reads and binds the enclosing function's names.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, name, 0, 0)`, the name that of the body's aside; `NOARG` slots are ignored by the assembler. The compiler emits the body as an aside, then RESET. `reset` emits count `0`; `reset-at` compiles its tag first, ahead of the aside, and emits count `1`.

**Example:** `(+ 1 (reset (* x 2)))` as the body of a function of `x`. The RESET's extra row: `(0, aside 0, 0, 0)`.

| index | opcode             | arg | arg view                  | comment |
|------:|--------------------|-----|---------------------------|---------|
| 0     | CONST              | 0   | 0 (1)                     | stack: [1] |
| 1     | PUSH_ASIDE         | 0   | 0 (None, aside 0)         | declare the body; go on at 5 |
| 2     | FAST_OPERAND_CONST | 1   | MULTIPLY, fast 0 ("x"), 2 | the body, reading the function's `x` |
| 3     | RETURN             | 1   | 1                         | the body's value; ends the prompt |
| 4     | POP_ASIDE          | 0   | 0                         | never run |
| 5     | RESET              | 2   | 2 (0, aside 0)            | push prompt; run the aside in a frame above it |
| 6     | OBJECT_OPERAND     | 2   | ADD x2                    | the function goes on with the reset's value |
| 7     | RETURN             | 1   | 1                         | |
