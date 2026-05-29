# compile/call/cc

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/call/cc`

**Syntax:** `(compile/call/cc FN)`

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

```scheme
;; Capture and immediately invoke: returns 42
(compile/call/cc
  (compile/lambda [k]
    (compile/call k 42)))

;; Escape continuation: short-circuit a computation
(compile/call/cc
  (compile/lambda [escape]
    (compile/begin
      (compile/call do-setup)
      (compile/call escape "early-exit")
      (compile/call do-cleanup))))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
