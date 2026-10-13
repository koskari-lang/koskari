# layout/syntax-rules
**Symbol:** `layout/syntax-rules`

**Use-case:** Block syntax for the `syntax-rules` transformer within a
`syntax` block. Converts indented pattern/template rule blocks into the
canonical `(syntax-rules [NAME] (LIT ...) (PATTERN TEMPLATE) ...)` form.

**Behaviour:** Accepts a literals list in the header (e.g. `[x]` or `(x y)`).
Each indented rule block has a pattern in the header and a template in the
body; a template of several forms is wrapped in `begin`. Does not accept peers.

A rule block header is either of:

- `case FORM:` -- `case` followed by exactly one form, which is the
  pattern. This is the spelling for a pattern most clearly written as a
  single parenthesized s-expression.
- `HEAD ARG...:` -- a header whose first element is a symbol, in which
  case the whole header is the pattern.

`ref (item obj name):` and `case (ref (item obj name)):` produce identical
output. Keyword atoms are valid in layout headers (see
[Reader and layout syntax](../reader-layout.md)); `case` is for grouping the
full pattern, not a workaround for keywords. A header that is a lone
parenthesized form, `(complex pat):`, raises
`LayoutError: rule must be a block`; write `case (complex pat):` instead.

**Named header:** `syntax-rules NAME [LIT ...]:` puts NAME in the emitted
form, which is where the transformer takes its identity for help text and
match errors. `syntax` and `extend-syntax` supply that name
themselves, so the named header belongs to a transformer bound directly,
as `var compile/NAME:`.

**Syntax:**

```
syntax-rules [LIT ...]:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN):
    TEMPLATE...
  PATTERN2 ARG:
    TEMPLATE2...

syntax-rules NAME [LIT ...]:
  case (PATTERN):
    TEMPLATE...
```

**Examples:**

```koskari
syntax-rules [x]:
  do-something-with-x:
    (print "no args" x)
    x
  do-something-with-x y:
    (print "one arg" x y)
    (+ x y)
;; -> (syntax-rules [x]
;;     ((do-something-with-x) (begin (print "no args" x) x))
;;     ((do-something-with-x y) (begin (print "one arg" x y) (+ x y))))

syntax-rules [item]:
  case (ref (item obj name)):
    (ref-item obj name)
;; -> (syntax-rules [item] ((ref (item obj name)) (ref-item obj name)))
;; same as ref (item obj name): ...
```

```koskari
var compile/swap:
  syntax-rules swap []:
    case (swap a b):
      (print b a)

(swap 1 2)
;; prints: 2 1
```
