# compile/parameterize
**Symbol:** `compile/parameterize`

**Syntax:** `(parameterize BINDINGS BODY ...)`

**Bindings:** Same as let: list of `[SYM EXPR]` or `(EXPR as: SYM)`; parsed with
`gather_bindings`.

**Use-case:** Temporarily rebind dynamic parameters for the duration of the
body. Parameters are read by `parameterq`, by the `parameter` builtin, or
through a `ParamRef`. When the body runs those reads see the parameterized
values; when the block exits the previous bindings are restored. At least one
binding is required.

**Behaviour:** Compiles each value expression onto the stack (first binding
deepest, last at TOS), emits PUSH_PARAM with a constant names tuple, compiles the
body with `helper_begin`, then emits POP_PARAM. Uses the VM's PARAM extent;
restore on exit (and on unwind) is handled by the evaluator.

**Task isolation:** A `parameterize` extent binds in the running context's own
table, and never in the shared root scope. A coroutine, defer, or task captures
the currently-applied bindings when it is created (the `defer` is the call site;
`create-task` is transport), and from then on its extents bind and restore in
its own table. Two concurrently-driven tasks never observe or disturb each
other's bindings, even when they interleave across awaits, and a task abandoned
mid-extent cannot leak its bindings anywhere. A `set!` on a parameter inside the
extent writes the extent's binding and is discarded with it when the block
exits.

**Root scope from Python:** Koskari code called from Python shares one root
parameter scope per thread. The scope is created on first use and lives as
long as the thread, so a store made with no `parameterize` in the way during
one call is read by every later call on that thread, through any function,
and by Python through a `ParamRef`. Another thread starts with a root scope of
its own and never sees these stores. Inside a `with Domain():` block the
block's `Domain` supplies the root scope instead; after the block, the
thread's scope answers again with its earlier stores.

**Examples:**

```koskari
;; Parameterize format; consumer pretty reads it via (parameterq format)
(parameterize [[format "woot %r woot"]]
  (pretty 100))

;; Multiple parameters
(parameterize [[a 100] [b 101]]
  (check_both))
```
