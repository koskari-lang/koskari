# compile/quote-syntax
**Symbol:** `compile/quote-syntax`

**Syntax:** `(quote-syntax FORM)`, or `#'FORM`; in layout mode `#'HEAD: BODY`
quotes an indented block (see [Block quote](compile-quote-block.md))

**Use-case:** Produce FORM as a literal value the way `quote` does, keeping
the hygiene marks on its symbols. `quote` strips them, which is right for
data: a `'(t)` written in a template yields the plain symbol `t`, the one
the caller also writes. A syntax transformer's patterns and templates are
not data the program compares but source the expander reads back, so
`define-syntax` quotes them with `quote-syntax`. A template that a
macro-generating macro introduced then keeps the outer expansion's marks,
and its references resolve to what that expansion bound. A macro author
writes `#'` for the same reason in the rare case of hand-building such a
form; the procedural counterpart is
[`quasiquote-syntax`](compile-quasiquote-syntax.md).

**Behaviour:** One argument required. A non-nil pair is rewritten to a list
of `quote-syntax` elements combined with `cons`, so each element is quoted
the same way; any other datum is emitted as a constant as it is.

**Examples:**

```koskari
(quote-syntax x)
#'(a b c)
```

Outside a transformer the two forms are the same as `quote`: nothing marks
the symbols in hand-written source.

```koskari
>>> #'(a b)
(a b)
```

**See also:** [`quote`](compile-quote.md),
[`quasiquote-syntax`](compile-quasiquote-syntax.md),
[Hygiene](../../guide/hygiene.md).
