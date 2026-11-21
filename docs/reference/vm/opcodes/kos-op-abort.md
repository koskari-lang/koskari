# KOS_OP_ABORT
- **Style**: EXTRA (`COUNT`, `ASIDE`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = the aside that is the handler. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Pops the tag when the count is `1`. One walk from the top of the dynamic chain locates the nearest matching prompt (a generator boundary denies). ABORT then aborts to that prompt: every node above it is left, the frame ABORT ran in among them (a dyn-wind after thunk and a `dyn-with` exit run; a `try` and a plain `with` are dropped without running their `finally` body or exit). When an after thunk or exit raises, the abort is abandoned and the raise propagates from where it ran, with the region's operands still in place. Once the prompt is at the top of the chain, a frame that runs the aside (see [Asides](../index.md#asides)) is attached above it with an empty stack. The handler's return ends the prompt and is the RESET's value, as the body's return would have been. Nothing is captured.
- **Pushes:** The handler's KOS_DYN_FRAME above the prompt, once the abort has left the region, so an ABORT or a SHIFT in the handler finds the same prompt. The frame holds the aside and its namespace itself, since the frame the ABORT ran in is gone by then.
- **Invariants**: The handler runs over the namespace live at the ABORT: it shares its names with the function the abort is written in.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a generator boundary on the way raises `KoskariPromptUnreachable` with the boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`, `abort`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, aside, 0, 0)`; `NOARG` slots are ignored by the assembler. `abort` emits count `0`; `abort-at` compiles its tag first and emits count `1`. Nothing comes back to an ABORT, so whatever the enclosing form emits after it is never reached.

**Example:** `(reset (+ 1 (abort 5)))` as a function's whole body.

| index | opcode | arg | arg view         | comment |
|------:|--------|-----|------------------|---------|
| 0     | RESET  | 0   | 0 (0, aside c#0) | push prompt; run the body above it |
| 1     | RETURN | 1   | 1                | the function's value, the reset's |

The reset's body, the aside at `c#0`. The ABORT's extra row: `(0, aside c#1, 0, 0)`.

| index | opcode         | arg | arg view         | comment |
|------:|----------------|-----|------------------|---------|
| 0     | CONST          | 0   | 0 (1)            | region operand; stack: [1] |
| 1     | ABORT          | 0   | 0 (0, aside c#1) | abort to the prompt; run the handler above it |
| 2     | OBJECT_OPERAND | 2   | ADD x2           | never reached |
| 3     | RETURN         | 1   | 1                | |

The handler, the body's aside at `c#1`:

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | RETURN_CONST | 0   | 0 (5)    | the handler's value; ends the prompt |
