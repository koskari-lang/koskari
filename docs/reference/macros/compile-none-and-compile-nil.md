# compile/None? and compile/nil?
**Syntax:** `(None? VALUE)`, `(nil? VALUE)`

**Behaviour:** Identity predicates for the **`None`** and **`nil`** singletons. Expand to
**`(compile/is VALUE None)`** and **`(compile/is VALUE nil)`**.

