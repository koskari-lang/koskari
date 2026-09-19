# compile/compileq

**Syntax:** `(compileq FORM)`, `(compileq FORM NS)`

**Behaviour:** Expands to `(call compile (quote FORM) ...)`, so the form
is written as surface syntax and compiled without running. With one
argument the namespace is `(locals)`; with two, the given namespace.
The result is an `Expression`, callable to run it.

**Examples:**

```koskari
koskari> (define e (compileq (+ 1 2)))
koskari> (type e)
<class 'koskari.lib.Expression'>
koskari> (e)
3
```

**See also:** [`compile`](../runtime/compile.md),
[`evaluate`](compile-evaluate.md).
