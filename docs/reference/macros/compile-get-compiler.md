# compile/get-compiler

**Syntax:** `(get-compiler)`, `(get-compiler NS)`

**Behaviour:** Returns the `Compiler` that owns the namespace's module,
for compiling further forms into it, as the
[`get-compiler`](../runtime/get-compiler.md) runtime function does. With
no argument the namespace is `(vars)`; with one, the given namespace.

**Examples:**

```koskari
>>> (type (get-compiler))
<class 'koskari.compile.compiler.Compiler'>
```

**See also:** [`compileq`](compile-compileq.md),
[`evaluate`](compile-evaluate.md).

**Also:** function [`get-compiler`](../runtime/functions.md#compilation-and-expansion).
