# compile/special-match
**Symbol:** `compile/special-match`

**Syntax:** `(special-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect special rules (same **`build-special`**
lowering as **`compile/special-rules`** — same overall shape as
**`compile/macro-match`**). Rule bodies receive **`compiler`** and
matcher slots, as for **`compile/special-rules`**.

**Behaviour:** Each rule’s pattern column must be **`(compiler-formal pattern)`**;
the inner **`pattern`** is lowered by **`parse_explicit_rule_pattern`** (explicit
MATCH syntax). If that inner pattern is a bare symbol, it is wrapped as
**`(REST sym)`** before parsing (same convenience as **`compile/macro-match`**).
Emits **`build-special`** with the lowered pattern and rule lambda pairs.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_special_match`
