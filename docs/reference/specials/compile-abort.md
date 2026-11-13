# compile/abort
**Symbol:** `compile/abort`

**Syntax:** `(abort BODY ...)`

**Use-case:** Leave everything up to the nearest
[`reset`](compile-reset.md) and give the `reset` a value. It is a
[`shift`](compile-shift.md) that binds no continuation: the rest of the
region is discarded, and nothing is captured, so it costs less than a
`shift` whose `K` goes unused. It serves an early exit that crosses
function calls, a dead branch of a search, and an effect whose handler
does not resume.

**Behaviour:** Emits `ABORT 0`, then the handler `BODY` inline, ending in
a return. The region between the `abort` and the `reset` is left first,
and the handler then runs in a frame of its own, with the `reset`'s prompt
still in place, over the names of the scope the `abort` is written in. Its
value is the value of the `reset`.

What [`shift`](compile-shift.md) says of its handler holds here, less the
continuation:

- A `dyn-wind` around the `abort` runs its after thunk and a `dyn-with`
  exits before the handler runs; when that after thunk or exit raises, the
  handler does not run and the region carries on where the raise is
  caught. A `finally` body and a plain `with` inside the region do not
  run on the abort.
- A `try` inside the region does not catch what `BODY` raises, and a
  `parameterize` inside the region is not in effect for it.
- A `shift` or another `abort` in `BODY` finds the same `reset`.
- A `return` in `BODY` returns from the handler, so its value is the
  value of the `reset`. A `break` or `continue` reaches only a loop
  written inside `BODY`. A `yield` or `yield-from` in `BODY` is refused
  at compile time with `SpecialError`.
- A `var` in `BODY` is a name of the scope the `abort` is written in; see
  [Names the handler binds](compile-shift.md#names-the-handler-binds).

In a copy of a region resumed by a call of a `shift`'s `K`, an `abort`
goes to that copy's prompt, and the handler's value is the value of the
`(k ...)` call.

**Errors:** Both are under the `KoskariContinuationError` category (see
[Error codes](../error-codes.md)) and carry `tag` (`None`, the default
tag) and `operation` (`abort`):

- `KoskariPromptNotFound` -- no `reset` anywhere on the dynamic chain:
  `abort has no enclosing reset`.
- `KoskariPromptUnreachable` -- a generator stands between the `abort`
  and the `reset`: `cannot abort across a generator boundary`. Its
  `boundary` attribute is `generator`.

An `abort` made inside a running `__enter__`, `__exit__`, before thunk or
after thunk is allowed, where a `shift` is refused: there is nothing to
capture. An `abort` in a before thunk leaves its `dyn-wind` unentered,
so the after thunk does not run.

**Examples:**

```koskari
;; the rest of the region is discarded
(reset (+ 1 (abort 5)))
;; => 5

;; an early exit across function calls
def search [node want]:
    when (== node.key want):
        abort node
    for-each [kid node.kids]:
        search kid want

def find [tree want]:
    reset:
        search tree want
        None

;; the after thunk runs before the handler
def order []:
    var log #[]
    reset:
        (dyn-wind (log.append "before")
                  (abort (log.append "handler"))
                  (log.append "after"))
    log
;; (order) => ["before", "after", "handler"]
```
