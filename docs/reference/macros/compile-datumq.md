# compile/datumq

**Syntax:** `(datumq FORM)`, `(datumq FORM POSITION)`, or `-'FORM`

**Behaviour:** Wraps FORM in a `Datum` without evaluating it, recording the
source position. It is the quoting counterpart of [`datum`](compile-datum.md):
`datum` captures the value of its argument, `datumq` captures the argument form
itself, expanding to `(datum (quote FORM) POSITION)`.

Without an explicit POSITION, the position recorded is FORM's own, or `None`
when the form carries none.

A `Datum` carries the form on its `datum` attribute and the position on
`position`. Hygiene leaves what is inside unmarked, and the compiler
positions errors in it from the position.

**In a template:** in a `syntax-rules` or `syntax-match` template,
`(datumq FORM)` writes FORM raw: the expansion emits it unmarked, with no
pattern variable substituted inside it. A template uses it for a name it
shares with the use site, such as a module-level slot another macro reads.
It takes exactly one form, and its position is where the template wrote the
form. Each expansion consumes one level, so a template that defines another
macro writes `(datumq (datumq name))`, and one that must write a literal
`datumq` call writes `((datumq datumq) name)`.

`-'FORM` is reader shorthand for `(datumq FORM)`, and `-'-'FORM` for the
nested form.

**Examples:**

```koskari
>>> var d (datumq (+ 1 2))
>>> d.datum
(+ 1 2)
```

```koskari
(syntax set-slot
  (syntax-rules [] [(set-slot v) (var -'shared-slot v)]))

(set-slot 7)
shared-slot
;; => 7
```

**See also:** [`datum`](compile-datum.md),
[`compile/quote`](../specials/compile-quote.md),
[Macros and hygiene](../../guide/macros-and-hygiene.md),
[Hygiene](../../guide/hygiene.md#writing-a-name-the-use-site-shares).
