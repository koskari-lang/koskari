# compile/special-rules
**Symbol:** `compile/special-rules`

**Syntax:** `(special-rules NAME LITERALS RULE ...)`

**Use-case:** A **`special-rules`**-style transformer whose rules are
compiler-aware procedures. Bind the result with **`compile/define`**.

**Behaviour:** Each rule's pattern column must be **`(compiler-formal pattern)`**:
the first subform names the compiler parameter, the second is a nested
**RULE** pattern with the literals and pattern variables of
**`compile/syntax-rules`**; the literals include **`NAME`**. The body of the
first rule whose pattern matches runs with the compiler bound to
**`compiler-formal`** and each pattern variable bound to what it matched, and
its result is the compile result.

**Also:** layout handler [`special-rules`](../layouts/layout-special-rules.md).
