# compile/datum

**Syntax:** `(datum EXPR)`, `(datum EXPR POSITION)`, inside a macro body.

**Behaviour:** Expands to `(call datum EXPR [POSITION])`, wrapping the
value in a `Datum` that carries a source position. In a procedural
macro, a symbol the body computes is emitted through `datum` so hygiene
treats it as user syntax rather than a fresh template identifier, and so
the compiler can position errors in it.

**Examples:**

```koskari
>>> (macroexpand-1 '(datum foo))
(call datum foo None)
```

```koskari
define-syntax job:
  macro-rules []:
    case (job tp rest ...):
      ;; rewrite (job kafka ...) to (job/kafka ...)
      (cons (datum (symbol f"job/{tp.datum}")) *: rest nil)
```

`rest` is the captured tail, spliced with `*:` into the variadic `cons`
and closed with `nil`. Passed as a single argument it would be consed on
as one element.

**See also:** [Macros and hygiene](../../guide/macros-and-hygiene.md),
[`koskari.hygiene`](../api/koskari-hygiene.md).
