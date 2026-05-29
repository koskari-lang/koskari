# compile/unquote-splicing
**Symbol:** `compile/unquote-splicing`

**Syntax:** `,@EXPR` (reader shorthand) or `(unquote-splicing EXPR)`

**Use-case:** Splice a list into a quasi-quoted list. Only valid in list
(element) position inside a `compile/quasiquote` body; the argument must
evaluate to a pair list or nil at runtime, and its elements are inserted
into the surrounding list.

**Behaviour:** When called as a top-level special form (outside quasiquote),
raises `SpecialError`. When the enclosing list is being compiled, splice sites
are detected by the quasiquote compiler and compiled directly; the spliced
value is pushed onto the stack as a `PAIR_SPLICE` argument. Not valid in the
cdr (dotted tail) position of an improper list; the quasiquote compiler raises
`SpecialError` if a splice form appears there.

**Examples:**

```scheme
`(a ,@items b)   ; items (a list) is spliced in between a and b
`(,@xs ,@ys)     ; two lists concatenated
```

