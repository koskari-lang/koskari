# A generator's return value

A Koskari generator that ends with `(return v)` has a return value,
and the evaluator delivers it: the generator's RETURN reaches
`iter_deliver_to_resume` (`eval.c`), which raises `StopIteration(v)`
into whatever is stepping it. From there the value survives on some
consumer surfaces and is dropped on others. This note records where it
goes today, and is the starting point for a design in which the value
is carried through every path that could hand it on, `yield-from` first
among them.

**See also:** [yield-from-close.md](yield-from-close.md) (the close
side of the same relay),
[generator-extent-freeze.md](generator-extent-freeze.md),
[demand-flattening.md](../archive/demand-flattening.md),
[../language/generators.md](../language/generators.md).

## What exists today

Probed 2026-11-07 over a Koskari generator `inner` yielding `1`, `2`
and ending `(return "done")`.

| Consumer | Sees the return value | Where |
| --- | --- | --- |
| Koskari `(next g)` on exhaustion | yes, `StopIteration.value` | `kos_raise_stop_iteration`, `eval.c` |
| Koskari `(next g default)` | no, the default | |
| Koskari `(list g)`, `for-each` | no | expected: the elements are the product |
| Koskari `(yield-from g)` | no, the form is always None | `yf_take_failure`, `opcode/YIELD_FROM.c`, drops the fetched StopIteration; the compiler emits a separate `CONST None` after `YIELD_FROM` |
| Python `next` / `send` on `iter(g)` | yes, `StopIteration.value` | |
| Python `yield from iter(g)`, `yield from g` | yes | |
| Python `__anext__` / `asend` | yes, as the `StopAsyncIteration` argument | `stepper_finish_exhausted`, `iterate.c` |
| Python `async for` | no | the Python protocol has no value here |

The same holds with the delegate kinds swapped: `(yield-from X)` is
None over a Python sync generator that returns a value, over a sync
iterator that raises `StopIteration(v)`, and over a Python generator
that catches a throw and returns. Python's `yield from` over each of
those receives the value.

Inside `YIELD_FROM.c` the value is already in hand or one fetch away at
every end: the sync generator's `res`, the immediate Koskari-iterator
`out`, and the pending StopIteration for the sync iterator and for the
flattened Koskari generator. Python async delegates have no return
value, and None is right for them.

The None is documented (`compile-yield-from.md`,
`kos-op-yield-from.md`, `vm/index.md`), so it is a contract and not a
defect. The conflict is the same page calling the form analogous to
Python's `yield from`, and the disagreement between a Python consumer
and a Koskari consumer of the same generator.

## Where the value is dropped

Known today:

- `yf_take_failure` releases the StopIteration without reading its
  value; `YF_FINISH` threads on without pushing.
- The transforms (`map`, `filter`, `zip`, `koskari.itertools`) step
  their sources through `kos_iter_step_provider`, which converts a
  source's StopIteration into the exhausted payload. Whether that
  payload is the source's return value or a mapped function's own
  StopIteration payload, and whether any transform forwards it, has
  not been traced.
- `for-each` and `(list g)` consume the end without a place to put it.
  That is the expected shape for an element consumer; the question is
  only whether a `for-each` with an else-style tail could want it.
- The `KosIter` stepping record on the async surface carries it to
  `StopAsyncIteration`, so the Python side keeps it; a Koskari `await`
  of `(g.__anext__)` goes through the foreign-awaitable path and has
  not been checked.

Suspected and to be traced: the context-level RETURN of a boundary
drive's generator, `demand` over a generator, and any resume kind that
takes a `KOS_ITER_EXHAUSTED` outcome and discards `out`.

## What the rule should be

Open. The candidate rule: a generator's return value is a first-class
result, delivered to any consumer that reads the end, and `yield-from`
evaluates to it. Under that rule:

- `(yield-from X)` pushes the delegate's return value, None for a
  Python async delegate. `yf_take_failure` hands the value back,
  `YF_FINISH` pushes it before threading on (the shape of
  `iter_next_push_resume` in `eval/eval_iter.c`), the two raw
  `EVAL_NORMAL` returns on the async path push None first, and the
  compiler stops emitting `CONST None`. The opcode stays NOARG; its
  stack effect becomes pop 1, push 1.
- The `.kio` bytecode ABI minor bumps: an old stream's separate None
  instruction would leave one extra value per delegation on the stack,
  and the exact-match header check refuses it instead.
- A generator whose last form is `(yield-from X)` returns X's return
  value, where Python's returns None. Koskari's last-expression value
  makes it forward; decide whether that is wanted.
- Statement-position `(yield-from X)` gains a `POP 1`, one instruction
  per delegation.
- One test pins "nothing pushed" (`tests/lib/opcode/test_yield_from.py`,
  the completion-then-underrun case); three others say in their
  docstrings that the value is not surfaced but observe `RETURN 0`.
  The corpus has no record of the form's value either way.

Before adopting the rule, the drop sites above need a full trace so
the value carries across every path at once, not through `yield-from`
alone.

## Open questions

- Which transforms, if any, should forward a source's return value,
  and what a `zip` of two returning generators would deliver.
- Whether `(next g default)` should distinguish the end from a default.
- Whether a statement-position `yield-from` should keep its `POP`, or
  the compiler should fold it as it folds the `CONST None; POP` pair
  today.
- Whether `(yield-from EXPR send: VALUE)`, documented in
  `language/generators.md` and unimplemented, is still wanted.

# The end.
