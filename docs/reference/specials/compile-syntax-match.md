# compile/syntax-match
**Symbol:** `compile/syntax-match`

**Syntax:** `(syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules (same emission path as
**`compile/syntax-rules`**: **`build-syntax-rules`**).

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`** where **`PATTERN`** is
lowered by **`parse_explicit_rule_pattern`** in `koskari/syntax.py` (explicit
matcher declarations such as **`(VALUE x)`**, **`(REPEAT … elem: …)`**, keyword
options on registered matchers, etc.). See [Macro matching and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.matcher`](../api/koskari-matcher.md) (`parse_explicit_rule_pattern`,
surface heads).

**Implementation:** `koskari/compile/specials/syntax.py`  -- 
`special_compile_syntax_match`


