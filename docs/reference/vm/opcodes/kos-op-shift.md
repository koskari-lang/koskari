# KOS_OP_SHIFT
- **Style**: EXTRA (`COUNT`, `CONST`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = the tag count, `0` for the default prompt tag or `1` for a tag the instruction pops from the stack, where a popped `None` is the default tag, and `a1` = constant pool index of the handler's **function spec**, a function of one parameter, the continuation. Any other count is refused when the code is built. Tags match by identity.
- **Stack**: Builds the handler from its spec against the current frame namespace, as BUILD_FUNCTION does. Captures the continuation: one walk from the top of the dynamic chain locates the nearest matching prompt while offering handle(SHIFT) to each node crossed (a generator boundary denies). The captured continuation is the delimited region with the shift expression as its hole, resuming at the instruction after SHIFT -- a cloned chain segment, so a shift hosted in a callee captures through the calling frames up to the prompt. The shift then aborts to that prompt: every node above it is left (a dyn-wind after thunk and a `dyn-with` exit run; a `try` and a plain `with` are dropped without running their `finally` body or exit). When an after thunk or exit raises, the abort is abandoned and the raise propagates from where it ran, with the region's operands still in place. Once the prompt is at the top of the chain its operand segment is emptied, the frame's pc is the prompt's POP_PROMPT, and the handler is called with the continuation above the prompt. The handler's return pushes its value on the prompt's segment, and POP_PROMPT takes it as the reset's value.
- **Pushes:** Nothing on the dynamic chain; the handler's frame is a call above the prompt, so a SHIFT in the handler finds the same prompt.
- **Invariants**: Invoking the captured continuation re-delimits: each invocation installs a copy of the region under a fresh prompt instance, so a shift inside that run aborts only to that copy's prompt, and its handler's value is the value of that invocation.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a node that denies handle(SHIFT) (a generator boundary) raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `tag` (the prompt tag sought) and `operation`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(count, function_spec, 0, 0)`; `NOARG` slots are ignored by the assembler. `shift` emits count `0`; `shift-at` compiles its tag first and emits count `1`.

**Example:** `(reset (+ 1 (shift k (k 5))))`. Pool: 0 = 1, 1 = the handler's function spec; extra row: `(0, fspec, 0, 0)`.

| index | opcode      | arg | arg view     | comment |
|------:|-------------|-----|--------------|---------|
| 0     | PUSH_PROMPT | 0   | 0            | push prompt (owning its segment) onto chain |
| 1     | CONST       | 0   | 0 (1)        | region operand; stack: [1] |
| 2     | SHIFT       | 0   | 0 (0, fspec) | capture k; abort to the prompt; call the handler with k |
| 3     | OBJECT_ADD  | 2   | 2            | the continuation resumes here with the hole's value |
| 4     | POP_PROMPT  | 0   | 0            | the handler's value, or the region's, is the reset's |
