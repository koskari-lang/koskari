# Python reflexes versus Koskari behaviour

Python and Scheme habits that compile and then misbehave in Koskari.
Each table row: the reflex, what it does, what to write. The sections
on [silent errors](#silently-wrong) and [raising errors](#raises) come
after the native builtins below.

## Native builtins versus their python namesakes

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

## Silently wrong

These run without error and return a wrong answer.

A pair is one link: it unpacks to its head and tail, `car` and `cdr`,
and `len` of any pair but `nil` is 2. `(.length x)` follows a proper
chain and counts its elements.

| Reflex | Result | Write |
|---|---|---|
| `(list? '(1 2))` to test for a list | `False`: `list?` is the Python `list` type test | `(proper? x)` or `(pair? x)` |
| `(len '(1 2 3))` | `2`: a pair is one link of two fields | `(.length x)` |
| `(list '(1 2 3))`, `for-each` over a pair chain | `#[1 (2 3)]` | `(list (unpack x))`, `for-each` over `(unpack x)` |
| `(cons 0 *: chain nil)` to prepend | splices the head and the tail of the first link | `(cons 0 chain)`: the last argument of `cons` is the tail |
| `(pair? x)` to mean non-empty | `nil` passes `pair?` | test `(nil? x)` first |
| `(list (py-filter pred xs))` with a Koskari `pred` | every element passes | `(filter pred xs)`, or `(py-filter (demand pred) xs)` |

## Raises

| Reflex | Error | Write |
|---|---|---|
| `#{1 2}` as a set literal | `ValueError`: `#{...}` is a dict | `(set #[1 2])` or `(#set 1 2)` |
| `(map f xs ys)` | `TypeError`: `map` takes one iterable | `(map (lambda [p] (f *: p)) (zip xs ys))` |
| `(apply f a b args)`, Scheme style | `TypeError`: `apply` is `(apply FUNC [ARGS [KWARGS]])` | `(apply f #[a b *: args])` |
| `(map not xs)`, `(apply % ...)` | `NameError`: `%`, `~`, `not`, `truth`, `divmod`, `is`, and `is-not` are special forms with no runtime binding | a lambda, such as `(lambda [x] (not x))`; `is?` and `is-not?` for identity |
| `(max xs key: (lambda ...))`, and `min` | `TypeError` comparing coroutines: `max` and `min` do not wrap the key | `key: (demand f)`; `sorted` wraps its key itself |
| `(#dict "a" 1)` | `ValueError`: items are pairs | `(#dict #("a" 1))` or `(#dict a: 1)` |

## Head position versus value

A name that heads a form compiles as its special form, whose syntax can
differ from calling the same name as a value.

| Head form | As a value |
|---|---|
| `(parameter NAME [DEFAULT])` with **NAME** unquoted; `(parameter 'p 3)` raises `SyntaxMatchError` | `(apply parameter #['p 3])`, the name quoted |
| `(next IT DEFAULT)` or `(next IT default: DEFAULT)` | `(apply next #[it 0])` |

## Catching

| Reflex | Fact | Write |
|---|---|---|
| `[[KoskariException] ...]` around a demand call | `KoskariSyncViolation` subclasses `TypeError` only | `[[KoskariSyncViolation] ...]` or `[[TypeError] ...]` |
| `[[SyntaxError] ...]` around `evaluate` or `compile` | compile errors such as `SyntaxMatchError` are `CompilerError`, not `SyntaxError` | `[[CompilerError] ...]` |
| `[[RuntimeError] ...]` around `shift` | continuation errors are not `RuntimeError` | `[[KoskariContinuationError] ...]` |

## Async handles

| Reflex | Fact | Write |
|---|---|---|
| awaiting one `defer` twice | the second await raises `RuntimeError`: a defer is single-shot | `(create-task (defer ...))` for a handle awaited more than once |
| `(await (create-task ...))` on the demand path | `KoskariSyncViolation`: the demand path cannot suspend | await it from async-hosted code |

Reference: [Functions](../reference/runtime/functions.md),
[Types and type predicates](../reference/runtime/types-and-predicates.md),
[Operators](../reference/runtime/operators.md),
[Error guide](../reference/error-codes.md).
