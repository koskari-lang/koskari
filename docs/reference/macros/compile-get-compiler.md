# compile/get-compiler

**Syntax:** `(get-compiler)`, `(get-compiler NS)`

**Behaviour:** Expands to a call to the
[`get-compiler`](../runtime/get-compiler.md) runtime function. With no
argument the namespace is `(vars)`; with one, the given namespace. The
result is the `Compiler` that owns the namespace's module, for compiling
further forms into it.

**Examples:**

```koskari
>>> (type (get-compiler))
<class 'koskari.compile.compiler.Compiler'>
```

**See also:** [`compileq`](compile-compileq.md),
[`evaluate`](compile-evaluate.md).
