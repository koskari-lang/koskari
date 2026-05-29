# compile/let/cc

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/let/cc`

**Syntax:** `(compile/let/cc K BODY ...)`

**Use-case:** Koskari-specific undelimited continuation capture. Pushes a new
child namespace, captures the current continuation and binds it to the name `K`
inside that namespace, then evaluates the body forms sequentially. If the
continuation bound to `K` is invoked later, control resumes just after the
`compile/let/cc` form with the supplied value. This is the idiomatic way to
name a continuation for later use without wrapping the body in a lambda.

**Behaviour:** Emits `PUSH_CC`, then opens a `child_namespace` that declares
`K` as a local. Inside the namespace: `CAPTURE_CC` captures the continuation
and pushes it; `VAR_STORE K` binds it to the declared name; the body is
compiled with `helper_begin`. Finally `POP_CC` closes the CC block outside the
namespace. `K` is not visible after the `compile/let/cc` form.

**Examples:**

```scheme
;; Bind escape continuation k, use it inside the body
(compile/let/cc k
  (compile/begin
    (compile/call do-step-one)
    (compile/call k "done")
    (compile/call never-reached)))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
