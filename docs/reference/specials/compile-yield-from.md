# compile/yield-from
**Symbol:** `compile/yield-from`

**Syntax:** `(yield-from EXPR)` -- exactly one argument.

**Use-case:** Delegate to another iterable or async-iterable and yield each
produced value through the current async generator, analogous to Python's
`yield from`. Prefer this over `compile/for-each` plus `compile/yield` when you
only forward an entire sequence (for example `(items data)` for
`dict.items()`).

**Behaviour:** Compiles `EXPR` onto the stack, emits `YIELD_FROM` with operand
`0`, then emits `CONST None`, so the form's value is `None`. At runtime the
delegate may be a sync iterator or generator, a Python async iterator or async
generator, or a Koskari generator; a bare coroutine is refused. Each value the
delegate produces is yielded through the current generator until the delegate
is exhausted, and send, throw and close reach the delegate. Like
`compile/yield`, a `yield-from` makes the enclosing function a generator.
Opcode behaviour is documented in
[YIELD_FROM](../vm/opcodes/kos-op-yield-from.md).

**Examples:**

```koskari
(yield-from (items data))

;; Same consumer-visible values as:
(for-each [pair (items data)]
  (yield pair))
```
