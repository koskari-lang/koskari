# compile/macro-match
**Symbol:** `compile/macro-match`

**Syntax:** `(macro-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect procedural rules, as **`compile/macro-rules`**
takes **RULE**-dialect ones.

**Behaviour:** As [`compile/macro-rules`](compile-macro-rules.md), except
that there is no `LITERALS` list, each pattern is in the explicit matcher
dialect (see [`koskari.matcher`](../api/koskari-matcher.md)), and the body
takes one parameter per pattern slot, in pattern order, bound to `None`
when the match left the slot unbound. A pattern head spelling `NAME` is a
slot bound to the symbol the macro was invoked as, so a renamed import
still matches; any other head is a literal.

**Also:** layout handler [`macro-match`](../layouts/layout-macro-match.md).
