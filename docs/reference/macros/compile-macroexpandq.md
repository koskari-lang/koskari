# compile/macroexpandq

**Syntax:** `(macroexpandq form)`, `(macroexpandq form ns)`

**Behaviour:** Expands to **`(call macroexpand (quote form) …)`**. With one argument,
the namespace is **`(globals)`**; with two, the given namespace is used. The form
is quoted before expansion, so you write surface syntax directly.

See [macroexpand](../runtime/macroexpand.md).
