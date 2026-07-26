# compile/define-parameter and compile/parameterq
**Syntax:** **`(define-parameter NAME [DEFAULT])`**, **`(parameterq NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`ParamRef`** binding; **`parameterq`** →
**`compile/parameter`**.

**Defaults declare, they do not store.** A `DEFAULT` is the answer a read gives
when nothing is bound. It belongs to the ref or the read site that names it and
never lands in a parameter store, so two places can declare different defaults
for one name without racing, and a read never changes what a later read sees.
Without a default, resolving an unbound name raises `NameError`.

**Where a value lives.** Resolution looks in the enclosing `parameterize` extent
first, then in the root scope of the current `Domain`. `parameterize` binds for
its extent only, in the running context's own table, so concurrent tasks never
see each other's bindings. A store made with no `parameterize` in the way lands
in the root scope, which the whole computation shares: that is the deliberate
channel for host pre-defines and for values a program sets once at startup.

**Across a fork.** Creating a callable with `defer` captures the extents applied
at that moment, so a task reads the bindings that were live when it was made,
not those the parent enters afterward. The root scope is not captured; it is
shared, so a store there is visible to parent and children alike.

**Scope guidance:** The bound `ParamRef` is a re-resolving accessor. Every read
and store finds the innermost binding of the context it runs in, so a
`define-parameter` binding is safe to use anywhere, including module scope and
closures. Reach for `parameterize` to bind for a dynamic extent, and for a plain
store when you want one value for the whole computation.
