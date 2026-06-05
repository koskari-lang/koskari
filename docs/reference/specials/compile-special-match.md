# compile/special-match
**Symbol:** `compile/special-match`

**Syntax:** `(special-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect special rules (same **`build-special-rules`**
lowering as **`compile/special-rules`**, with an empty literals tuple — same
overall shape as **`compile/macro-match`**). Rule bodies receive **`compiler`**,
**`tc`**, and matcher slots, as for **`compile/special-rules`**.

**Behaviour:** Each rule’s pattern column must be **`(compiler-formal pattern [tc-formal])`**;
the inner **`pattern`** is lowered by **`parse_explicit_rule_pattern`** (explicit
MATCH syntax). If that inner pattern is a bare symbol, it is wrapped as
**`(REST sym)`** before parsing (same convenience as **`compile/macro-match`**).
Emits **`build-special-rules`** with **`()`** for the serialized literals tuple.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_special_match`


