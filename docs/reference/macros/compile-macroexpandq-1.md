# compile/macroexpandq-1

**Syntax:** `(macroexpandq-1 FORM)`, `(macroexpandq-1 FORM NS)`

**Behaviour:** Expands to `(call macroexpand-1 (quote FORM) ...)`. The
form is quoted for you, so it is written as surface syntax, and one
expansion step is applied. With one argument the namespace is
`(globals)`; with two, the given namespace.

**Examples:**

```koskari
koskari> (macroexpandq-1 (when a b))
(cond [a b] [else None])
```

**See also:** [`macroexpand-1`](compile-macroexpand-1.md).
