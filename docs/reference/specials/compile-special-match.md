# compile/special-match
**Symbol:** `compile/special-match`

**Syntax:** `(special-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect special rules, as **`compile/special-rules`**
takes **RULE**-dialect ones.

**Behaviour:** As [`compile/special-rules`](compile-special-rules.md), except
that there is no `LITERALS` list, the inner pattern is in the explicit
matcher dialect, and the body takes one parameter per pattern slot after
the compiler, bound to `None` when unbound. An inner pattern head spelling
`NAME` is a slot bound to the symbol the special was invoked as, so a
renamed import still matches; any other head is a literal. A bare symbol as
the inner pattern matches any form and binds its elements under that name.
Pattern cells are read as in [`compile/syntax-match`](compile-syntax-match.md).

**Also:** layout handler [`special-match`](../layouts/layout-special-match.md).
