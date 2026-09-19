# compile/quote-syntax
**Symbol:** `compile/quote-syntax`

**Syntax:** `(quote-syntax FORM)`

**Use-case:** Produce FORM as a literal value the way `quote` does, keeping
the hygiene marks on its symbols and keywords. `quote` strips them, which is
right for data: a `'(t)` written in a template yields the plain symbol `t`.
A syntax transformer's patterns and templates are data the expander reads
back as source, so `define-syntax` quotes them with `quote-syntax`. A
template that a macro-generating macro introduced then keeps the outer
expansion's marks, and its references resolve to what that expansion bound.

**Behaviour:** One argument required. A non-nil pair is rewritten to a list
of `quote-syntax` elements combined with `cons`, so each element is quoted
the same way; any other datum is emitted as a constant as it is.

**Examples:**

```koskari
(quote-syntax x)
(quote-syntax (a b c))
```

Outside a transformer the two forms are the same: nothing marks the symbols
in hand-written source.

```koskari
>>> (quote-syntax (a b))
(a b)
```

**See also:** [`quote`](compile-quote.md),
[Macros and hygiene](../../guide/macros-and-hygiene.md).
