# compile/macro-rules
**Symbol:** `compile/macro-rules`

**Syntax:** `(macro-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`macro-rules`**-style transformer spec to a
**`build-macro-rules`** call form. Bind the result with **`compile/define`**.

**Behaviour:** Parses literals and implicit-pattern macro rules, emits
**`build-macro-rules`** with serialized pattern matchers and per-rule lambdas.
An arm's body returns the expansion; `None` is a valid expansion and compiles
to the constant `None`, so an arm that has nothing to say returns `None`
rather than an empty `(begin)`. An arm builds its output with a backtick,
which is data; a `macro-rules` transformer that a template defines builds
its output with `` #` `` instead, so the outer template's names keep their
marks (see [Hygiene](../../guide/hygiene.md)).

**See also:** [Macros and hygiene](../../guide/macros-and-hygiene.md),
[Hygiene](../../guide/hygiene.md),
[`quasiquote-syntax`](compile-quasiquote-syntax.md).
