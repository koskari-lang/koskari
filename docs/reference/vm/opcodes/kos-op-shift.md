# KOS_OP_SHIFT
- **Style**: EXTRA (`COUNT`, `ASIDE_NS`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = the aside that is the handler. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Pops the tag when the count is `1`, ahead of the capture, so the tag is no part of the continuation. One walk from the top of the dynamic chain locates the nearest matching prompt (a generator boundary denies) and checks that each node on the way can be captured. The continuation is the delimited region with the shift expression as its hole: every node from the top of the chain down to the prompt, so a shift hosted in a callee captures through the calling frames up to the prompt. SHIFT then aborts to the prompt as [ABORT](kos-op-abort.md) does, and the nodes that abort leaves are kept as the continuation instead of being freed. The frame attached above the prompt runs the aside (see [Asides](../index.md#asides)) with the continuation pushed on its stack. The handler's return ends the prompt and is the RESET's value, as the body's return would have been.
- **Pushes:** The handler's KOS_DYN_FRAME above the prompt, once the abort has left the region, so a shift in the handler finds the same prompt. The frame holds the aside and its namespace itself, since the frame the SHIFT ran in is part of the continuation by then.
- **Invariants**: The continuation holds whole frames up to and including the reset's body, and a copy of the prompt. Invoking it re-delimits: each invocation attaches a copy of them above the caller, so a shift inside that run aborts only to that copy's prompt, and the copy's body returns to the caller as a callee does; its value, or the value of a handler run above the copy's prompt, is the value of that invocation. The frame SHIFT ran in stands at the instruction after SHIFT, so each copy of it goes on there with the value the continuation was called with on its stack. The handler runs over a namespace built from its own NamespaceSpec. The compiler gives that spec one local, the continuation's name, and the handler's first instruction stores the continuation there: the name is the handler's alone, and each run of the handler has its own. Every other name the handler declares is the enclosing function's. A TRY or FINALLY region captured while it is handling an exception is handling that exception again in each copy, and gives the caller's back when the copy leaves the chain. The continuation reaches the handler only once the abort is complete: when a raising after thunk or exit abandons the abort, the continuation is discarded.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a generator boundary on the way, or a `with`, `dyn-with` or dyn-wind node whose enter, exit, before or after call is running, raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`, `shift`. A native frame on the way whose pending step cannot be copied raises `RuntimeError`. Each is raised before any node leaves the chain.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, aside, 0, 0)`; `NOARG` slots are ignored by the assembler. The handler's first instruction is a `VAR_STORE` of the continuation's name, which lowers to `FAST_STORE`. `shift` emits count `0`; `shift-at` compiles its tag first and emits count `1`.

**Example:** `(reset (+ 1 (shift k (k 5))))` as a function's whole body.

| index | opcode | arg | arg view         | comment |
|------:|--------|-----|------------------|---------|
| 0     | RESET  | 0   | 0 (0, aside c#0) | push prompt; run the body above it |
| 1     | RETURN | 1   | 1                | the function's value, the reset's |

The reset's body, the aside at `c#0`. The SHIFT's extra row: `(0, aside c#1, 0, 0)`.

| index | opcode         | arg | arg view         | comment |
|------:|----------------|-----|------------------|---------|
| 0     | CONST          | 0   | 0 (1)            | region operand; stack: [1] |
| 1     | SHIFT          | 0   | 0 (0, aside c#1) | capture; abort to the prompt; run the handler above it |
| 2     | OBJECT_OPERAND | 2   | ADD x2           | the continuation resumes here with the hole's value |
| 3     | RETURN         | 1   | 1                | the body's value, or a copy's, ends its prompt |

The handler, the body's aside at `c#1`. Its spec has `k` as its fast variable 0.

| index | opcode         | arg | arg view        | comment |
|------:|----------------|-----|-----------------|---------|
| 0     | FAST_STORE     | 0   | 0 (local "k")   | bind the continuation SHIFT pushed |
| 1     | CONST          | 0   | 0 (5)           | |
| 2     | TAIL_CALL_FAST | 0   | fast 0 ("k"), 1 | (k 5) |
| 3     | RETURN         | 1   | 1               | the handler's value; ends the prompt |
