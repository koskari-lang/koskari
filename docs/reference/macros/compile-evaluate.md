# compile/evaluate

**Syntax:** `(evaluate FORM)`, `(evaluate FORM NS)`

**Behaviour:** Compiles `FORM` if it is not already compiled and runs
it in the namespace, returning the value, as the
[`evaluate`](../runtime/evaluate.md) runtime function does. With one argument the namespace is `(vars)`;
with two, the given namespace, which is a Koskari namespace or module
rather than a plain dict. `FORM` is evaluated, so quote a literal form.

**Examples:**

```koskari
>>> (evaluate '(+ 1 2))
3
>>> (var x 4)
>>> (evaluate '(* 2 x) (globals))
8
```

**See also:** [`compileq`](compile-compileq.md) to compile without
running, [`get-compiler`](compile-get-compiler.md).

**Also:** function [`evaluate`](../runtime/functions.md#compilation-and-expansion).
