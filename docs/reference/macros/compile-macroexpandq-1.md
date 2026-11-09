# compile/macroexpandq-1

**Syntax:** `(macroexpandq-1 FORM)`, `(macroexpandq-1 FORM NS)`

**Behaviour:** Returns `FORM` after one expansion step, as
[`macroexpand-1`](compile-macroexpand-1.md) does. The form is quoted
for you, so it is written as surface syntax. With one argument the namespace is
`(globals)`; with two, the given namespace.

**Examples:**

```koskari
>>> (macroexpandq-1 (when a b))
(cond [a b] [else: None])
```

**See also:** [`macroexpand-1`](compile-macroexpand-1.md).
