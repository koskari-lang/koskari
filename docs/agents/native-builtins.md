# Native builtins versus their python namesakes

Several bootstrap names that look like python builtins are koskari
NATIVE types. They accept koskari functions directly -- no `demand`
wrapping -- and run them inside the evaluator, awaits and all. The
python originals remain available under `py-` names (or by import)
and keep the python calling convention, where a bare koskari function
produces un-awaited coroutines.

| name | is | python original | predicates |
|------|----|-----------------|------------|
| `map` | native `koskari.lib.map` | `py-map` | `map?` / `py-map?` |
| `filter` | native `koskari.lib.filter` | `py-filter` | `filter?` / `py-filter?` |
| `partial` | native `koskari.lib.partial` | `(import-from functools partial)` | `partial?` |
| `demand` | native wrapper type | -- | `demand?` |

The rule of thumb:

- A koskari callable handed to `map`, `filter`, or `partial` just
  works. Do not wrap it.
- A koskari callable handed to `py-map`, `py-filter`, `sorted`'s
  cousins, or any other python higher-order function needs `demand`
  around it, or each call returns a truthy un-awaited Coroutine.

## The trap, concretely

```koskari
(list (filter (lambda [x] False) #[1 2 3]))
; #[] -- native filter runs the predicate; nothing passes

(list (py-filter (lambda [x] False) #[1 2 3]))
; #[1 2 3] -- python's filter got a coroutine per element,
; coroutines are truthy, everything "passed"
```

The second form is silently wrong: no error, plausible-looking
output. The same shape applies to mapping:

```koskari
(list (map {(* %1 %1)} #[1 2 3]))
; #[1 4 9] -- native map, no wrapping

(list (py-map (demand {(* %1 %1)}) #[1 2 3]))
; #[1 4 9] -- python map works too, but only demand-wrapped
```

## What the natives add

Beyond taking koskari callables directly, the native `map` and
`filter` iterate from both surfaces: synchronously (`(list ...)`,
`for-each`, python `list()`) and asynchronously (`for-each` in an
asynchronous context, python `async for`), over any source -- a
sequence, a koskari generator, a python generator, or a python async
iterator. A function or predicate that awaits passes through
transparently in asynchronous consumption; a genuine suspension under
synchronous consumption raises `KoskariSyncViolation`.

`partial` composes with all of it: `(map (partial + 10) seq)` fixes
arguments on the native side with no convention crossing.

## When you actually want the python one

Interoperating with python code that type-checks against the builtin
(`isinstance(x, map)`), or feeding a python-defined callable where the
overhead question is python-to-python anyway. Reach for `py-map` /
`py-filter` deliberately, and wrap any koskari callable you hand them
in `demand`.

Reference: [map and filter](../reference/runtime/map-and-filter.md);
the demand rules in [Pitfalls](pitfalls.md).

## The same rule covers itertools

`import-from itertools ...` is almost always the wrong import to
generate. Use `koskari.itertools`, which publishes every combinator
under the name you already know:

```
accumulate  batched  chain  chain-from-iterable  compress  cycle
dropwhile  filterfalse  groupby  islice  pairwise  starmap
takewhile  zip-longest
```

`map`, `filter` and `zip` are NOT there -- they are bare in builtins
and already native. Do not import them from anywhere.

Why it matters for generated code: a koskari callable handed to
python's `itertools` returns a coroutine per call instead of a value,
and coroutines are truthy, so every predicate reads as true. This does
not raise. It produces plausible output that is wrong:

```koskari
(import-from itertools takewhile)
(list (takewhile (lambda [x] (< x 3)) #[1 2 3 4 1]))
; #[1 2 3 4 1] -- nothing was filtered, no error was raised

(import-from koskari.itertools takewhile)
(list (takewhile (lambda [x] (< x 3)) #[1 2 3 4 1]))
; #[1 2]
```

Both forms compile and both run. Only the second is correct, and
nothing in a test or a traceback will point at the first. Treat the
python import as a defect on sight.

Sources behave the same way as for `map` and `filter`: a suspending
source reaches these combinators and does not reach python's. The
consumer still decides -- `for-each` in an asynchronous context awaits
in place, `list` drives synchronously and raises
`KoskariSyncViolation` on a source that suspends.

Two departures from python worth knowing before you rely on them:
`groupby` yields each group as a list rather than a shared-cursor
view, so a group stays valid after the parent advances; and
`filterfalse` returns a `filter` carrying an inverted verdict, so
`filter?` answers true for it.

Reference: [koskari.itertools](../reference/runtime/koskari-itertools.md).
