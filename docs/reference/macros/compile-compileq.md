# compile/compileq

**Syntax:** `(compileq form)`, `(compileq form ns)`

**Behaviour:** Expands to **`(call compile (quote form) …)`**. With one argument,
the namespace is **`(locals)`**; with two, the given namespace is used.
