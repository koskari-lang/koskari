# compile/datumq

**Syntax:** `(datumq FORM)`, `(datumq FORM POSITION)`

**Behaviour:** Wraps FORM in a `Datum` without evaluating it, recording the
source position. It is the quoting counterpart of [`datum`](compile-datum.md):
`datum` captures the value of its argument, `datumq` captures the argument form
itself, expanding to `(datum (quote FORM) POSITION)`.

Without an explicit POSITION, the position recorded is FORM's own, or `None`
when the form carries none.

A `Datum` carries the form on its `datum` attribute and the position on
`position`. Hygiene leaves what is inside unmarked, and the compiler
positions errors in it from the position.

**Examples:**

```koskari
>>> define d (datumq (+ 1 2))
>>> d.datum
(+ 1 2)
```

**See also:** [`datum`](compile-datum.md),
[`compile/quote`](../specials/compile-quote.md),
[Macros and hygiene](../../guide/macros-and-hygiene.md).
