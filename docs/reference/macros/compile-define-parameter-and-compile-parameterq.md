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
`parameterize` in the way lands in the root scope, shared by the whole
computation: the channel for host pre-defines and for values set once at
startup. A `defer` awaited directly runs under the extents applied at
the await. A task created from it reads the bindings live at its
creation. The root scope stays shared either way.

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
