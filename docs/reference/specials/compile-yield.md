# compile/yield
**Symbol:** `compile/yield`

**Syntax:** `(yield VAL ...)` -- variadic.

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

At runtime, `yield` suspends the generator with its open extents kept,
not unwound, so no `finally` or `__exit__` runs at a yield. The value goes
to the generator's consumer: a `for-each`, a `next`, a delegating
`yield-from`, or a Python consumer. When the generator is resumed, the
`yield` form evaluates to the value sent in, or `None` when none is sent;
an exception thrown in is raised at the `yield`.

The body of a [`reset`](compile-reset.md) and the handler of a
[`shift`](compile-shift.md) are functions of their own and not part of a
generator around them: a `yield` written in either is refused at compile
time with `SpecialError`. A generator defined inside one is its own
function and yields as usual.

A `StopIteration` or `StopAsyncIteration` that escapes a generator's
body is not the generator's own end signal. As in Python (PEP 479, PEP
525), it leaves the generator as `RuntimeError("generator raised
StopIteration")` or `RuntimeError("generator raised
StopAsyncIteration")`, with the original exception as `__cause__`, on
every surface: a Python `next` or `list`, a Python `async for`, and a
Koskari `for-each` over the same generator. A function awaited from
Python that raises `StopIteration` likewise raises
`RuntimeError("coroutine raised StopIteration")`. A generator ends
normally only by returning; an unguarded `(next it)` in its body that
runs out is an error, not the end of the generator.

**Examples:**

```koskari
(yield)                     ;; -> None
(yield (compute-next))      ;; -> the computed value
(yield a b c)               ;; -> #(a b c)  (a tuple)
(yield a: a b: b c: c)      ;; -> values(a=a, b=b, c=c)
```
