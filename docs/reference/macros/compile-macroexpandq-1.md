# compile/macroexpandq-1

**Syntax:** `(macroexpandq-1 form)`, `(macroexpandq-1 form ns)`

**Behaviour:** Expands to **`(call macroexpand-1 (quote form) …)`**. With one argument,
the namespace is **`(globals)`**; with two, the given namespace is used. The form
is quoted before expansion, so you write surface syntax directly.

See [macroexpand-1](../runtime/macroexpand-1.md).
