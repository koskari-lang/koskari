# compile/syntax-match
**Symbol:** `compile/syntax-match`

**Syntax:** `(syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules, as **`compile/syntax-rules`**
takes **RULE**-dialect ones. The value is the transformer.

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`**. A pattern is
written in explicit matcher declarations such as **`(VALUE x)`**,
**`(REPEAT xs PATTERN min: 2)`**, and the keyword options a registered
matcher accepts. A use of the transformer expands the template of the first
rule whose pattern matches, with the pattern's slots bound to what they
matched. See
[Macros and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.matcher`](../api/koskari-matcher.md) (surface heads).
