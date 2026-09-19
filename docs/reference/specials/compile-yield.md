# compile/yield
**Symbol:** `compile/yield`

**Syntax:** `(yield VAL ...)` — variadic.

| Form               | Result                                          |
|--------------------|-------------------------------------------------|
| `(yield)`          | yields `None`                                   |
| `(yield VAL)`      | yields `VAL`                                     |
| `(yield VAL ...)`  | yields a `tuple` of the values                  |
| `(yield K: V ...)` | yields a `values` object over the keyword pairs |

**Use-case:** Yield from a generator (or coroutine). Suspends the current
frame and returns the value to the driver; resumption is via `send(value)` or
`throw()`. `yield` is variadic. With no argument it yields `None`; with one it
yields that value; with two or more positional arguments it yields a `tuple`;
and with keyword arguments it yields a multi-value `values` object.

**Bare form:** the lone symbol `yield` expands to `(yield)`, so the
Python-style spelling yields `None`. Governed by the
[`compiler/bare-control-words`](../../guide/pragma.md#compiler) pragma of the
module being compiled (`convert` by default, or `warn` / `error`); because the
bare symbol always expands, `yield` can never name a variable.

**Behaviour:** Compiles each positional argument and emits `YIELD N`, where `N`
is the argument count. `YIELD 0` yields `None`, `YIELD 1` yields the single
value, and `YIELD N` (for `N > 1`) packages the values as a tuple. A keyword
argument re-heads the form as `(values ...)`, so `(yield a: 1 b: 2)` yields a
`values` object; positional and keyword arguments must not be mixed.

At runtime, YIELD finds the running frame's generator node
(`kos_dyn_find_gen`) and freezes the chain above it into the node
(`kos_dyn_gen_freeze`); the open extents inside the generator are kept,
not unwound, so no `finally` or `__exit__` runs at a yield. When the
generator is consumed on the same chain (a `for-each`, a NEXT, or a
delegating `yield-from`), the value is delivered to that consumer and
evaluation continues there without leaving `koskari_eval`. When the
generator is driven from outside (a Python `next` or `send`), the opcode
returns `EVAL_YIELD(val)`, `SUSPENSION_EVENT = YIELD` is set, and
`koskari_eval` returns `KOS_RES_YIELD`. On resume, `revive_yield` pushes
`send_val` onto the stack.

**Examples:**

```koskari
(yield)                     ;; -> None
(yield (compute-next))      ;; -> the computed value
(yield a b c)               ;; -> #(a b c)  (a tuple)
(yield a: a b: b c: c)      ;; -> values(a=a, b=b, c=c)
```
