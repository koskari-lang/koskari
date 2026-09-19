# compile/let*

**Syntax:** `(let* [[NAME VALUE] ...] BODY ...)`

**Behaviour:** Sequential `let`. Each binding is evaluated in a scope
where the bindings before it are visible, by nesting one
[`let`](../specials/compile-let.md) per binding. A plain `let` evaluates
every value in the outer scope.

**Examples:**

```koskari
>>> (let* [[a 1] [b (+ a 1)]] (* a b))
2
```

```koskari
let* [[path (Path root)] [src (path.joinpath "src")]]:
  (src.exists)
```

**See also:** [`let`](../specials/compile-let.md),
[`let-values`](compile-let-values.md).
