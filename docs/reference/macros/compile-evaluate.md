# compile/evaluate

**Syntax:** `(evaluate FORM)`, `(evaluate FORM NS)`

**Behaviour:** Expands to a call to the
[`evaluate`](../runtime/evaluate.md) runtime function, which compiles
`FORM` if it is not already compiled and runs it in the namespace,
returning the value. With one argument the namespace is `(locals)`;
with two, the given namespace, which is a Koskari namespace or module
rather than a plain dict. `FORM` is evaluated, so quote a literal form.

**Examples:**

```koskari
koskari> (evaluate '(+ 1 2))
3
koskari> (define x 4)
koskari> (evaluate '(* 2 x) (globals))
8
```

**See also:** [`compileq`](compile-compileq.md) to compile without
running, [`get-compiler`](compile-get-compiler.md).
