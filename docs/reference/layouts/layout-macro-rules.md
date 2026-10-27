# layout/macro-rules
**Symbol:** `layout/macro-rules`

**Use-case:** Block syntax for the `macro-rules` transformer within a
`syntax` block. Same surface as `layout/syntax-rules` (literals list and
indented rules); lowers to
`(macro-rules [NAME] (LIT ...) (PATTERN BODY) ...)`. The rule body is a
procedural macro body rather than a template, and nested layout blocks
within it are expanded.

**Behaviour:** Same rule shape as
[`layout/syntax-rules`](layout-syntax-rules.md); only the emitted head and
the treatment of the rule body differ. A body of several forms is wrapped
in `begin`. Does not accept peers.

A rule block header is either `case FORM:`, where `case` is followed by
exactly one form and that form is the pattern, or a header whose first
element is a symbol, in which case the whole header is the pattern. A
header that is a lone parenthesized form, `(complex pat):`, raises
`LayoutError: rule must be a block`; write `case (complex pat):` instead.

**Named header:** `macro-rules NAME [LIT ...]:` puts NAME in the emitted
form, which is where the transformer takes its identity for help text and
match errors. `syntax` and `extend-syntax` supply that name
themselves, so the named header belongs to a transformer bound directly,
as `var compile/NAME:`.

**Syntax:**

```
macro-rules [LIT ...]:
  PATTERN1:
    BODY1...
  case (PATTERN2):
    BODY2...

macro-rules NAME [LIT ...]:
  case (PATTERN):
    BODY...
```

**Examples:**

```koskari
macro-rules [x]:
  case (foo a):
    (bar a)
    (baz a)
;; -> (macro-rules [x] ((foo a) (begin (bar a) (baz a))))
```

```koskari
var compile/twice:
  macro-rules twice []:
    case (twice x):
      `(begin ,x ,x)

(twice (print "hello"))
;; prints: hello
;; prints: hello
```
