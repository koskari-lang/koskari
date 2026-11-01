# compile/syntax-match
**Symbol:** `compile/syntax-match`

**Syntax:** `(syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules, as **`compile/syntax-rules`**
takes **RULE**-dialect ones. The value is the transformer.

**Behaviour:** As [`compile/syntax-rules`](compile-syntax-rules.md), except
that there is no `LITERALS` list and each pattern is written in explicit
matcher declarations such as **`(VALUE x)`** or
**`(REPEAT xs PATTERN min: 2)`**; the template refers to the pattern's
slots. A pattern head spelling `NAME` is a slot bound to the symbol the
macro was invoked as, so a renamed import still matches; any other head is
a literal. See
[Macros and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.matcher`](../api/koskari-matcher.md) (surface heads).

**Also:** layout handler [`syntax-match`](../layouts/layout-syntax-match.md).
