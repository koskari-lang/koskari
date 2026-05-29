# compile/macroexpand

**Syntax:** `(macroexpand form)`, `(macroexpand form ns)`

**Behaviour:** Expands to a call to the **`macroexpand`** runtime function. With one
argument, the namespace is **`(globals)`**; with two, the given namespace is used.

See [macroexpand](../runtime/macroexpand.md).
