# compile/define-parameter and compile/parameterq

**Syntax:** `(define-parameter NAME [DEFAULT])`, `(parameterq NAME [DEFAULT])`

**Behaviour:** `define-parameter` binds `NAME` to a `ParamRef`, a
re-resolving accessor: every read and store finds the innermost
[`parameterize`](../specials/compile-parameterize.md) binding of the
context it runs in, and the root scope of the current domain when there
is none. `parameterq` reads a parameter by name without a binding,
through [`parameter`](../specials/compile-parameter.md).

A default declares, it does not store. It is the answer a read gives
when nothing is bound, and it belongs to the ref or the read site that
names it, so two sites can declare different defaults without racing.
A read of an unbound name with no default raises `NameError`.

A value lives in the enclosing `parameterize` extent first, then in the
root scope. `parameterize` binds in the running context's own table, so
concurrent tasks never see each other's bindings. A store with no
`parameterize` in the way lands in the root scope, shared by every call
on the thread: the channel for host pre-defines and for values set once
at startup. A `defer` awaited directly runs under the extents applied at
the await. A task created from it reads the bindings live at its
creation. The root scope is the thread's either way.

**Root scope:** Koskari code called from Python shares one root parameter
scope per thread. The scope is created on first use and lives as long as
the thread, so a store made with no `parameterize` in the way during one
call is read by every later call on that thread, through any function,
and by Python through a `ParamRef`. Another thread starts with a root
scope of its own and never sees these stores. A coroutine or defer runs
under the root scope of the thread that resumes it, wherever it was
created; only its `parameterize` bindings travel with it. Inside a
`with Domain():` block the block's `Domain` supplies the root scope
instead, for Python reads and for Koskari calls made inside the block;
after the block, the thread's scope answers again with its earlier
stores. The block belongs to the running stack, a thread's synchronous
stack or one asyncio task, so blocks on different tasks never see each
other and a block can only be closed by the stack that opened it.

**Examples:**

```koskari
(define-parameter verbose False)

define show [] (if verbose "loud" "quiet")

(show)                                    ;; "quiet"
(parameterize [[verbose True]] (show))    ;; "loud"
(parameterq verbose False)                ;; False
```

```koskari
>>> (macroexpand-1 '(define-parameter verbose False))
(begin (define verbose) (bindq verbose (ParamRef (quote verbose) False)))
```

**See also:** [`parameterize`](../specials/compile-parameterize.md),
[`ref`](compile-ref.md) for `(ref (parameter NAME))`.
