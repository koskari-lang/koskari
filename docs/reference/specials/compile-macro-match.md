# compile/macro-match

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/macro-match`

**Syntax:** `(compile/macro-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect procedural rules (same emission shape as
**`compile/macro-rules`**: **`build-macro-rules`**).

**Behaviour:** Each rule row is **`(PATTERN BODY …)`**; **`PATTERN`** is passed to
**`parse_explicit_rule_pattern`**. Per-rule **`compile/lambda`** formals are
**`matcher_slot_names(pattern_matcher)`** in consume order (see
`koskari/matcher.py` and [MATCHER.md](../../design/MATCHER.md)). Macro procedures are invoked
with **keyword** arguments from the match result (`koskari/syntax.py`:
**`MacroRule.expand`**).

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_macro_match`

**Tests:** `tests/compile/specials/test_define_syntax.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
