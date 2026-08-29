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
