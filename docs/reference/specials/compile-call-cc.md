# compile/call/cc
**Symbol:** `compile/call/cc`

**Syntax:** `(call/cc FN)`

**Use-case:** Undelimited call-with-current-continuation (Scheme-like). Captures
the current continuation at the call site, wraps it in a `Continuation` object,
and immediately calls `FN` with that continuation as its sole argument. When the
continuation is invoked with a value, control returns to the point just after the
`compile/call/cc` form with that value. If `FN` returns normally without invoking
the continuation, the return value of `FN` becomes the value of the form.

The continuation packs its arguments the way `return` does: `(k)` resumes
with `None`, `(k v)` with `v`, `(k a b)` with the tuple `(a, b)`, and a call
with keywords, `(k a x: 1)`, with a `values` instance over all of them.

**Behaviour:** Emits `PUSH_CC` (opens a CC block), compiles `FN`, emits
`CAPTURE_CC` (captures the continuation and pushes it), emits `CALL 1` (calls
`FN` with the continuation), then `POP_CC` (closes the CC block on normal
return). The continuation may be invoked from Koskari code in the evaluation
that captured it, from Python code that evaluation calls, or from a nested
Koskari evaluation running under such Python code. A transfer from a nested
evaluation leaves it and any evaluation between it and the capturing one,
running the exit of each `dyn-with` in them with no exception, and resumes
the capturing evaluation with the value.

Invoking the continuation leaves every extent between the call site and the
capture point: the after thunk of a [`dyn-wind`](compile-dyn-wind.md) and
the exit of a [`dyn-with`](dyn-with.md) run, innermost first, before the
continuation's body resumes. The `finally:` of a [`try`](compile-try.md)
and the exit of a plain [`with`](with.md) are local to their body and do
not run on the transfer. Extents the continuation re-enters are entered
outermost first, each `dyn-wind` before thunk and `dyn-with` enter running
before anything inside it; when one raises, nothing inside it sees the
exception, and it reaches the call of the continuation as the extents
already entered unwind.

**Errors:** A `with`, `dyn-with` or `dyn-wind` inside the captured region
is captured with it. A capture taken from inside one of their calls while it runs, the body of
`__enter__` or `__exit__` or a before or after thunk, is refused, as is a
capture that would cross a generator: `KoskariPromptUnreachable`, with the
`boundary` attribute naming what refused (`enter`, `exit`, `before`,
`after` or `generator`) and `operation` set to `call/cc`. The same applies
to [`let/cc`](compile-let-cc.md) and [`capture/cc`](compile-capture-cc.md).
Invoking the continuation while the evaluation that captured it is not
running -- it has returned, or it is suspended on an `await` -- raises
`KoskariContinuationDead`; each top-level form of a module is its own
evaluation. See [Error codes](../error-codes.md).

**Examples:**

```koskari
;; Capture and immediately invoke: returns 42
(call/cc
  (lambda [k]
    (k 42)))

;; Escape continuation: short-circuit a computation
(call/cc
  (lambda [escape]
    (begin
      (do-setup)
      (escape "early-exit")
      (do-cleanup))))

;; Separate top-level forms: by the try, the var form's evaluation has returned
(import-from koskari.lib KoskariContinuationDead)
(var k (call/cc (lambda [c] c)))
(try (k 2)
  [[e KoskariContinuationDead] (str e)])
;; "continuation target context is no longer alive"
```
