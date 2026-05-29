# compile/evaluate

**Syntax:** `(evaluate form)`, `(evaluate form ns)`

**Behaviour:** Expands to a call to the **`evaluate`** runtime function. With one
argument, the namespace is **`(locals)`**; with two, the given namespace is used.
