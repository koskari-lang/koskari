# compile/macro-rules
**Symbol:** `compile/macro-rules`

**Syntax:** `(macro-rules NAME LITERALS RULE ...)`

**Use-case:** A **`macro-rules`**-style transformer whose rules are
procedures. Bind the result with **`compile/define`**.

**Behaviour:** Each rule row is **`(PATTERN BODY ...)`**, with the literals
and pattern variables of **`compile/syntax-rules`**. The body of the first
rule whose pattern matches runs with each pattern variable bound to what it
matched. An arm's body returns the expansion; `None` is a valid expansion and compiles
to the constant `None`, so an arm that has nothing to say returns `None`
rather than an empty `(begin)`. An arm builds its output with a backtick,
which is data; a `macro-rules` transformer that a template defines builds
its output with `` #` `` instead, so the outer template's names keep their
marks (see [Hygiene](../../guide/hygiene.md)).

**See also:** [Macros and hygiene](../../guide/macros-and-hygiene.md),
[Hygiene](../../guide/hygiene.md),
[`quasiquote-syntax`](compile-quasiquote-syntax.md).

**Also:** layout handler [`macro-rules`](../layouts/layout-macro-rules.md).
