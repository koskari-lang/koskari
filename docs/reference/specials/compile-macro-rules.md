# compile/macro-rules

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/macro-rules`

**Syntax:** `(compile/macro-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`macro-rules`**-style transformer spec to a
**`build-macro-rules`** call form. Bind the result with **`compile/define`**.

**Behaviour:** Parses literals and implicit-pattern macro rules, emits
**`build-macro-rules`** with serialized pattern matchers and per-rule lambdas.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_macro_rules`

**Tests:** `tests/compile/specials/test_define_syntax.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
