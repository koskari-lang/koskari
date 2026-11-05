# compile/datum

**Syntax:** `(datum EXPR)`, `(datum EXPR POSITION)`, inside a macro body.

**Behaviour:** Wraps the value of `EXPR` in a `Datum` that carries a
source position, passing `POSITION` along when given, as the `datum`
function does. In a procedural
macro, a symbol the body computes is emitted through `datum` so hygiene
treats it as user syntax rather than a fresh template identifier, and so
the compiler can position errors in it.

**Examples:**

```koskari
syntax job:
  macro-rules []:
    case (job tp rest ...):
      ;; rewrite (job kafka ...) to (job/kafka ...)
      (cons (datum (symbol f"job/{tp.datum.base}")) *: rest nil)
```

The type tag is a label, so the name is built from its base; a tag a
template wrote carries the expansion's mark, which `str` would show.

`rest` is the captured tail, spliced with `*:` into the variadic `cons`
and closed with `nil`. Passed as a single argument it would be consed on
as one element.

**See also:** [Macros and hygiene](../../guide/macros-and-hygiene.md),
[`koskari.hygiene`](../api/koskari-hygiene.md).

**Also:** type [`datum`](../runtime/types-and-predicates.md#pairs-and-symbols).
