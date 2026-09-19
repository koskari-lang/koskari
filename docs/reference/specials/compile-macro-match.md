# compile/macro-match
**Symbol:** `compile/macro-match`

**Syntax:** `(macro-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect procedural rules (same emission shape as
**`compile/macro-rules`**: **`build-macro-rules`**).

**Behaviour:** Each rule row is **`(PATTERN BODY …)`**; **`PATTERN`** is passed to
**`parse_explicit_rule_pattern`**. Per-rule **`compile/lambda`** formals are
**`matcher_slot_names(pattern_matcher)`** in consume order (see
[`koskari.matcher`](../api/koskari-matcher.md)). Macro procedures are invoked
with **keyword** arguments from the match result (`koskari/syntax.py`:
**`MacroRule.expand`**).

**Implementation:** `koskari/compile/specials/syntax.py`  -- 
`special_compile_macro_match`


