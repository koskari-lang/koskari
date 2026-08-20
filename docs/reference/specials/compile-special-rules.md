# compile/special-rules
**Symbol:** `compile/special-rules`

**Syntax:** `(special-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`special-rules`**-style transformer spec to a
**`build-special`** call form. Rule procedures are **compiler-aware**:
each rule’s lambda is built with formals **`(COMPILER pattern-vars …)`** —
the **`COMPILER`** slot corresponds to the pattern column breakout
(see behaviour). Bind the result with **`compile/define`**.

**Behaviour:** Like **`compile/macro-rules`**, but each rule’s pattern column must
be **`(compiler-formal pattern)`**: the first subform names the
compiler parameter, the second is a nested **RULE** pattern (lowered with
**`parse_rule_pattern`**). Per-rule **`compile/lambda`** formals are
**`compiler-formal`**, then pattern variables from the inner
RULE pattern (same ordering as **`compile/macro-rules`** for implicit patterns).
The literal set includes the macro **`NAME`** automatically (as for other
rule-based transformers). The built transformer’s **`compile_impl`** passes
**`compiler`** and the captured pattern variables into the winning rule and
returns its result for recompilation.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_special_rules`
