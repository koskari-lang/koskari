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
return). The continuation may be invoked from within the same eval context
(direct apply) or from Python code in a frame above the owning eval loop
(upwards apply via `KoskariContinuationApply`). Cross-context invocation from
a different eval context triggers the segmented-transfer path.

Invoking the continuation leaves every extent between the call site and the
capture point: the `finally:` of a [`try`](compile-try.md), the `__exit__`
of a [`with`](with.md) and the after thunk of a
[`dyn-wind`](compile-dyn-wind.md) all run, innermost first, before the
continuation's body resumes.

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
```
