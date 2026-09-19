# compile/call/cc
**Symbol:** `compile/call/cc`

**Syntax:** `(call/cc FN)`

**Use-case:** Undelimited call-with-current-continuation (Scheme-like). Captures
the current continuation at the call site, wraps it in a `Continuation` object,
and immediately calls `FN` with that continuation as its sole argument. When the
continuation is invoked with a value, control returns to the point just after the
`compile/call/cc` form with that value. If `FN` returns normally without invoking
the continuation, the return value of `FN` becomes the value of the form.

**Behaviour:** Emits `PUSH_CC` (opens a CC block), compiles `FN`, emits
`CAPTURE_CC` (captures the continuation and pushes it), emits `CALL 1` (calls
`FN` with the continuation), then `POP_CC` (closes the CC block on normal
return). The continuation may be invoked from within the same eval context
(direct apply) or from Python code in a frame above the owning eval loop
(upwards apply via `KoskariContinuationApply`). Cross-context invocation from
a different eval context triggers the segmented-transfer path.

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
