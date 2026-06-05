# KOS_OP_INVOKE_K_VAR
- **Style**: CONST. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: Pops value; looks up continuation by name; invokes it with value. Same push model as INVOKE_K; prompt need not be in chain.
- **Invariants**: Name must be bound to a continuation (e.g. from shift handler).
- **Compiler form:** Pre-compile arg = name (string/symbol); post-compile arg = pool index.

**Example:** Invoke continuation bound to name "k". Pool: 0 = "k".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 1   | 1 (99)   | stack: [..., 99] |
| 1     | INVOKE_K_VAR | 0 | 0 ('k')  | pop 99; lookup "k"; invoke k(99) |


## 21. Undelimited continuations (call/cc)

CAPTURE_CC captures the continuation of the CC dynamic extent (from the current
pc until the extent is exited). Invoking that continuation is a **non-local
jump to the POP_CC opcode**, with a value pushed onto the stack; POP_CC then
runs (pops the CC block) and execution continues at the next instruction (the
PUSH_CC resume target).

