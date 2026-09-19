# compile/macro-rules
**Symbol:** `compile/macro-rules`

**Syntax:** `(macro-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`macro-rules`**-style transformer spec to a
**`build-macro-rules`** call form. Bind the result with **`compile/define`**.

**Behaviour:** Parses literals and implicit-pattern macro rules, emits
**`build-macro-rules`** with serialized pattern matchers and per-rule lambdas.
An arm's body returns the expansion; `None` is a valid expansion and compiles
to the constant `None`, so an arm that has nothing to say returns `None`
rather than an empty `(begin)`.
