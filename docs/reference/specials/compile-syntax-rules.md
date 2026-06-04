# compile/syntax-rules
**Symbol:** `compile/syntax-rules`

**Syntax:** `(syntax-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`syntax-rules`**-style transformer spec to a
**`build-syntax-rules`** call form (for recompilation). The built transformer is
the **value** of that expression; bind it with **`(define NAME …)`**.

**Behaviour:** Parses literals and implicit-pattern rules, builds a
`SyntaxRulesTransformer`, emits **`(build-syntax-rules NAME TAGGED)`**.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_syntax_rules`


**See also:** [Macro matching and hygiene](../concepts/macro-matching-and-hygiene.md)
— **`compile/syntax-rules`** hygiene patterns (`DEFINE`, `BIND` for literals,
`REST` for rules).

