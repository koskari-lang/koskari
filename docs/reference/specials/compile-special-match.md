# compile/special-match
**Symbol:** `compile/special-match`

**Syntax:** `(special-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect special rules, as **`compile/special-rules`**
takes **RULE**-dialect ones. Rule bodies receive **`compiler`** and the
matcher slots, as for **`compile/macro-match`**.

**Behaviour:** Each rule's pattern column must be **`(compiler-formal pattern)`**,
the inner **`pattern`** in the explicit matcher dialect. A bare symbol as the
inner pattern matches any form and binds its elements under that name. The
body of the first rule whose pattern matches runs with the compiler bound to
**`compiler-formal`** and one parameter per pattern slot, and its result is
the compile result.
