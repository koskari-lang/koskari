# compile/syntax-rules

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/syntax-rules`

**Syntax:** `(compile/syntax-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`syntax-rules`**-style transformer spec to a
**`build-syntax-rules`** call form (for recompilation). The built transformer is
the **value** of that expression; bind it with **`(compile/define NAME …)`**.

**Behaviour:** Parses literals and implicit-pattern rules, builds a
`SyntaxRulesTransformer`, emits **`(build-syntax-rules NAME TAGGED)`**.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_syntax_rules`

**Tests:** `tests/compile/specials/test_define_syntax.py`

**See also:** [MATCHER.md](../../design/MATCHER.md) — **`compile/syntax-rules`** hygiene
patterns (`DEFINE`, `BIND` for literals, `REST` for rules).

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
