# KOS_OP_CDR
- **Style**: COUNT. Arg = repeat count: apply **cdr** that many times to the
  stack top (each step pops one pair and pushes its cdr).
- **Invariants**: **`n` ≥ 1**. **`n` ≤ 0** (including **CDR 0**) is invalid:
  **RuntimeError** (`CDR invoked with invalid argument count: …`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` ≥ 1**, same effect as **`n`** chained **CDR 1**
  instructions (one stack pop, local walk, one push). If any intermediate value
  is not a pair or is nil, **TypeError** (message includes step index and type /
  `nil`).

