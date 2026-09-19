# layout/macro-match
**Symbol:** `layout/macro-match`

**Use-case:** Block syntax for the `macro-match` transformer within a
`define-syntax` block. No literals list; each rule carries an explicit
match-style pattern and a procedural body, and lowers to
`(macro-match [NAME] (PATTERN BODY) ...)`.

**Behaviour:** Each rule block supplies the pattern from its header and
the macro body from its body; a body of several forms is wrapped in
`begin`, and nested layout blocks within a body are expanded. Does not
accept peers.

A rule block header is either of:

- `case FORM:` -- `case` followed by exactly one form, which is the
  pattern. This is the spelling for a pattern most clearly written as a
  single parenthesized s-expression.
- `HEAD ARG...:` -- a header whose first element is a symbol, in which
  case the whole header is the pattern.

A rule may also be written on one line as `(PATTERN BODY...)`, or as the
elements `PATTERN BODY...` spread across one line; either way the
pattern's own first element must be a symbol.

A header that is a lone parenthesized form, `(complex pat):`, raises
`LayoutError: rule must be a block`, because the header's first element
is then a pair rather than a symbol. Write `case (complex pat):` instead.

**Named header:** `macro-match NAME:` puts NAME in the emitted form,
which is where the transformer takes its identity for help text and match
errors. `define-syntax` and `extend-syntax` supply that name themselves,
so the named header belongs to a transformer bound directly, as
`define compile/NAME:`.

**Syntax:**

```
macro-match:
  case (PATTERN):
    BODY...
  HEAD ARGS...:
    BODY...

macro-match NAME:
  case (PATTERN):
    BODY...
```

**Examples:**

```koskari
macro-match:
  case (foo a):
    (bar a)
    (baz a)
;; -> (macro-match ((foo a) (begin (bar a) (baz a))))
```

```koskari
define compile/pair-up:
  macro-match pair-up:
    case (pair-up (BIND a) (BIND b)):
      `(print ,b ,a)

(pair-up 3 4)
;; prints: 4 3
```
