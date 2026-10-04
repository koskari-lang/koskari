# compile/syntax-match
**Symbol:** `compile/syntax-match`

**Syntax:** `(syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules, on the same emission path as
**`compile/syntax-rules`**: a **`build-syntax`** call form whose value is the
transformer.

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`**. The special emits
**`(build-syntax "NAME" (parse-match-pattern (quote-syntax PATTERN))
(quote-syntax TEMPLATE) ...)`**; `parse-match-pattern` lowers the pattern
through `parse_explicit_rule_pattern` in `koskari.matcher` when the
expression is evaluated. A pattern is written in explicit matcher
declarations such as **`(VALUE x)`**, **`(REPEAT xs PATTERN min: 2)`**, and
the keyword options a registered matcher accepts. See
[Macros and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.matcher`](../api/koskari-matcher.md) (`parse_explicit_rule_pattern`,
surface heads).
