# compile/syntax-match

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/syntax-match`

**Syntax:** `(compile/syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules (same emission path as
**`compile/syntax-rules`**: **`build-syntax-rules`**).

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`** where **`PATTERN`** is
lowered by **`parse_explicit_rule_pattern`** in `koskari/syntax.py` (explicit
matcher declarations such as **`(VALUE x)`**, **`(REPEAT … elem: …)`**, keyword
options on registered matchers, etc.). See
**[language/unified-matcher-transformer-model.md](../../design/language/unified-matcher-transformer-model.md)**
and **[MATCHER.md](../../design/MATCHER.md)** (`parse_explicit_rule_pattern`, surface heads).

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_syntax_match`

**Tests:** `tests/compile/specials/test_define_syntax.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
