# compile/yield-from
**Symbol:** `compile/yield-from`

**Syntax:** `(yield-from EXPR)` — exactly one argument.

**Use-case:** Delegate to another iterable or async-iterable and yield each
produced value through the current async generator, analogous to Python's
`yield from`. Prefer this over `compile/for-each` plus `compile/yield` when you
only forward an entire sequence (for example `(items data)` for
`dict.items()`).

**Behaviour:** Compiles `EXPR` onto the stack, emits `YIELD_FROM` with operand
`0`, then emits `CONST None` so control can continue after delegation completes
(see `koskari/compile/specials/continuations.py`). At runtime the opcode
consumes the iterable value on TOS, classifies it (sync iterator or generator,
Python async iterator or async generator, Koskari async generator), rejects bare
coroutines, and drives the delegate until exhaustion while forwarding yields and
send/throw/close semantics. Like `compile/yield`, participating code is marked
with `KOS_CODE_YIELDS`. Full stack and suspension details are in
`design/OPCODES.md` (KOS_OP_YIELD_FROM) and `koskari/lib/opcode/YIELD_FROM.c`.

**Examples:**

```scheme
(yield-from (items data))

;; Same consumer-visible values as:
(for-each [pair (items data)]
  (yield pair))
```


