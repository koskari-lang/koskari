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

**Pattern cells:** within a rule's pattern, an atom is a literal. A list
headed by a registered matcher head -- `(VALUE x)`, `(LIST ...)`,
`(OPTIONAL ...)`, and so on -- is always that matcher, with or without a
slot name: `(h (LIST (SYMBOL k)))` matches `(h (a))` and binds `k` to
`a`. A malformed matcher head, such as `(VALUE 1)`, is refused when the
rule is defined. Any other list is an inline group whose cells are matched
in sequence where it stands: `(h (foo (VALUE x)))` matches `(h foo 1)`,
not `(h (foo 1))`. To match a matcher head's symbol literally, write
`(LITERAL_SYM NAME)`.

**Also:** layout handler [`syntax-match`](../layouts/layout-syntax-match.md).
