# compile/quasiquote-syntax
**Symbol:** `compile/quasiquote-syntax`

**Syntax:** `(quasiquote-syntax FORM)`, or `` #`FORM ``; in layout mode
`` #`HEAD: BODY `` quotes an indented block (see
[Block quasiquote](compile-quasiquote-block.md))

**Use-case:** Build a form the way `quasiquote` does, keeping the hygiene
marks on the symbols it writes as constants. A backtick is data: a
symbol a template wrote under it comes out as the plain symbol, so
`(== kind 'ok)` in a template compares against the caller's `ok`. The one
place that is wrong is a procedural macro that a template defines. Its
body was written by the outer template, so its symbols carry the outer
expansion's marks, and its output is source for a later expansion, not
data; a plain backtick there projects the marks away and the output
resolves at the use site instead of to what the outer expansion bound.
`` #` `` keeps them.

**Behaviour:** One argument required. Identical to `quasiquote` in every
respect but one: a scoped symbol is emitted as itself rather than as the
symbol it spells. `unquote` and `unquote-splicing` sites are evaluated as
in `quasiquote`, and a nested `quasiquote` is reconstructed as data at the
deeper level.

**Examples:**

```koskari
(syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (var secret val)
       (global syntax name (macro-rules [] [(name) #`secret])))]))

(var secret 100)
(mk-getter get-secret 8)
(get-secret)
;; => 8
```

With a plain backtick in the inner macro, `(get-secret)` is 100: the
handler's `secret` lost the outer mark and resolved to the module's
`secret`.

Outside a template the two forms are the same, since nothing marks the
symbols in hand-written source:

```koskari
>>> #`(a ,(+ 1 1))
(a 2)
```

**See also:** [`quasiquote`](compile-quasiquote.md),
[`quote-syntax`](compile-quote-syntax.md),
[Hygiene](../../guide/hygiene.md).
