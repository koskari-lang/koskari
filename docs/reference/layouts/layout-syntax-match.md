# layout/syntax-match
**Symbol:** `layout/syntax-match`

**Use-case:** Block syntax for the `syntax-match` transformer within a
`syntax` block. No literals list; each rule carries an explicit
match-style pattern and lowers to `(syntax-match [NAME] (PATTERN TEMPLATE) ...)`.

**Behaviour:** Same per-rule header and body rules as
[`layout/syntax-rules`](layout-syntax-rules.md), without the literals list
in the header. A template of several forms is wrapped in `begin`. Does not
accept peers.

A rule block header is either `case FORM:`, where `case` is followed by
exactly one form and that form is the pattern, or a header whose first
element is a symbol, in which case the whole header is the pattern. A
header that is a lone parenthesized form, `(complex pat):`, raises
`LayoutError: rule must be a block`; write `case (complex pat):` instead.

**Named header:** `syntax-match NAME:` puts NAME in the emitted form,
which is where the transformer takes its identity for help text and match
errors. `syntax` and `extend-syntax` supply that name themselves,
so the named header belongs to a transformer bound directly, as
`var compile/NAME:`.

**Syntax:**

```
syntax-match:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN2):
    TEMPLATE2...

syntax-match NAME:
  case (PATTERN):
    TEMPLATE...
```

**Examples:**

```koskari
syntax-match:
  case (foo a):
    (tmpl a)
;; -> (syntax-match ((foo a) (tmpl a)))
```

```koskari
var compile/first-of:
  syntax-match first-of:
    case (first-of (BIND a) (BIND b)):
      (print a)

(first-of 5 6)
;; prints: 5
```

**Also:** special form [`syntax-match`](../specials/compile-syntax-match.md).
