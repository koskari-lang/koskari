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

**Root scope:** A read that finds no `parameterize` binding falls through to
the root scope, one per thread or per `with Domain():` block; see
[`define-parameter`](../macros/compile-define-parameter-and-compile-parameterq.md).

**Parameterize from Python:** `koskari.types.parameterize` opens the same
extent over a block of Python code: `with parameterize(width=5):`, or
`with parameterize({"line-width": 5}):` for a name that is not a Python
identifier. Inside the block a read from Python or from Koskari code called
within it sees the binding; a block opened by Python code that Koskari called
out to is inner to the calling extent. A coroutine, task, or asyncio task
created inside the block keeps the bindings applied at its creation. The block
belongs to the running stack, a thread's synchronous stack or one asyncio
task, so blocks on different tasks never see each other and each exit
restores only what that block bound. A store inside the block writes the
block's binding, not the root scope.

**Examples:**

```koskari
;; Parameterize format; consumer pretty reads it via (parameterq format)
(parameterize [[format "woot %r woot"]]
  (pretty 100))

;; Multiple parameters
(parameterize [[a 100] [b 101]]
  (check_both))
```
