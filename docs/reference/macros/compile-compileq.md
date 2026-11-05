# compile/compileq

**Syntax:** `(compileq FORM)`, `(compileq FORM NS)`

**Behaviour:** Compiles `FORM` without running it, as the
[`compile`](../runtime/compile.md) function does. `FORM` is not
evaluated, so it is written as surface syntax. With one
argument the namespace is `(vars)`; with two, the given namespace.
The result is an `Expression`, callable to run it.

**Examples:**

```koskari
>>> (var e (compileq (+ 1 2)))
>>> (type e)
<class 'koskari.lib.Expression'>
>>> (e)
3
```

**See also:** [`compile`](../runtime/compile.md),
[`evaluate`](compile-evaluate.md).
