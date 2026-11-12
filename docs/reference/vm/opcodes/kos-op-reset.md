# KOS_OP_RESET
- **Style**: EXTRA (`COUNT`, `CONST`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = constant pool index of the body's **function spec**, a function of no arguments. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Builds the body from its spec against the current frame namespace, as BUILD_FUNCTION does, pushes a prompt carrying the tag, and calls the body above the prompt as CALL calls a function. The body's return, or the return of a shift's handler called above the same prompt, unlinks the prompt and pushes the value on the caller's stack, as a call's return does; the frame goes on at the instruction after RESET.
- **Pushes:** KOS_DYN_PROMPT, then the body's KOS_DYN_FRAME above it. The prompt owns no operand segment: the region's operands are in the frames above it.
- **Invariants**: SHIFT captures up to this prompt. A prompt at the root of a context ends the context when the frame on it returns. An exception unwinding past a PROMPT unlinks it.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, function_spec, 0, 0)`; `NOARG` slots are ignored by the assembler. `reset` emits count `0`; `reset-at` compiles its tag first and emits count `1`.

**Example:** `(reset (+ 1 (shift k (k 5))))`. The caller's extra row: `(0, body fspec, 0, 0)`.

| index | opcode      | arg | arg view          | comment |
|------:|-------------|-----|-------------------|---------|
| 0     | RESET       | 0   | 0 (0, body fspec) | push prompt; call the body above it |
| 1     | RETURN      | 1   | 1                 | the reset's value |
