# compile/parameterize
**Symbol:** `compile/parameterize`

**Syntax:** `(parameterize BINDINGS BODY ...)`

**Bindings:** Same as let: list of `[SYM EXPR]` or `(EXPR as: SYM)`; parsed with
`gather_bindings`.

**Use-case:** Temporarily rebind dynamic parameters for the duration of the
body. Parameters are consumed by functions that read them (e.g. via
ParamRef(name).deref(); bind-param not yet implemented). When the body runs,
calls to such functions see the parameterized values; when the block exits,
parameters are restored.

**Behaviour:** Compiles each value expression onto the stack (first binding
deepest, last at TOS), emits PUSH_PARAM with a constant names tuple, compiles the
body with `helper_begin`, then emits POP_PARAM. Uses the VM's PARAM extent;
restore on exit (and on unwind) is handled by the evaluator.

**Task isolation:** Parameter bindings are per-context. A coroutine, defer,
or task captures the currently-applied bindings when it is created (the
`defer` is the call site; `create-task` is transport), and from then on its
extents bind and restore in its own table. Two concurrently-driven tasks
never observe or disturb each other's `parameterize` bindings, even when
they interleave across awaits, and a task abandoned mid-extent cannot leak
its bindings anywhere. A `set!` on a parameter inside the extent writes the
extent's binding and is discarded with it when the block exits.

**Examples:**

```koskari
;; Parameterize format; consumer pretty reads it via ParamRef("format").deref()
(parameterize [[format "woot %r woot"]]
  (pretty 100))

;; Multiple parameters
(parameterize [[a 100] [b 101]]
  (check_both))
```

