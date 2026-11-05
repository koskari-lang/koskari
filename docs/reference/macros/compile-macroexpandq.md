# compile/macroexpandq

**Syntax:** `(macroexpandq FORM)`, `(macroexpandq FORM NS)`

**Behaviour:** Returns `FORM` fully expanded, as
[`macroexpand`](compile-macroexpand.md) does. The form is quoted for
you, so it is written as surface syntax. With one argument
the namespace is `(globals)`; with two, the given namespace.

**Examples:**

```koskari
>>> (macroexpandq (when a b))
(cond [a b] [else: None])
```

**See also:** [`macroexpand`](compile-macroexpand.md).
