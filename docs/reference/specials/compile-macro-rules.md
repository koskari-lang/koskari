# compile/macro-rules
**Symbol:** `compile/macro-rules`

**Syntax:** `(macro-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`macro-rules`**-style transformer spec to a
**`build-macro-rules`** call form. Bind the result with **`compile/define`**.

**Behaviour:** Parses literals and implicit-pattern macro rules, emits
**`build-macro-rules`** with serialized pattern matchers and per-rule lambdas.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_macro_rules`


