# KOS_OP_CAR
- **Style**: COUNT. Arg = repeat count: apply **car** that many times to the
  stack top (each step pops one pair and pushes its car).
- **Status**: Implemented.
- **Invariants**: **`n` ≥ 1**. **`n` ≤ 0** (including **CAR 0**) is invalid:
  **RuntimeError** (`CAR invoked with invalid argument count: …`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` ≥ 1**, same effect as **`n`** chained **CAR 1**
  instructions (one stack pop of the initial value, local walk, one push). If
  any intermediate value is not a pair or is nil, **TypeError** (message
  includes step index and type / `nil`).

