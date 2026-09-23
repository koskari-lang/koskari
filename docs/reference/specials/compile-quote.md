# compile/quote
**Symbol:** `compile/quote`

**Syntax:** `(quote FORM)`

**Use-case:** Prevent evaluation; FORM is produced as a literal value. For
atoms, a single CONST is emitted. For non-nil lists, the form is rewritten to
(cons (quote e1) (quote e2) ...) and that form is
compiled so each element is quoted and combined via the CONS opcode.

**Behaviour:** One argument required. If FORM is a non-nil pair, rewrite with
`requote` to a list of quoted elements (using compile/cons and compile/quote), set
position on the result, and return that pair so the compiler re-dispatches and
compiles it. Otherwise emit CONST FORM. A symbol a macro template wrote is
quoted as the plain symbol, without its hygiene mark; `quote-syntax` keeps the
mark.

**Examples:**

```koskari
(quote 42)
(quote x)
(quote (a b c))
```

**See also:** [`quote-syntax`](compile-quote-syntax.md),
[`quasiquote`](compile-quasiquote.md), [Hygiene](../../guide/hygiene.md).
