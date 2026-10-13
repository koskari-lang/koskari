# layout/special-rules
**Symbol:** `layout/special-rules`

**Use-case:** Block syntax for the `special-rules` transformer, normally
the inner block of a `syntax` or `extend-syntax` block. Converts
indented rule blocks into the canonical
`(special-rules [NAME] (LIT ...) (PATTERN BODY) ...)` form that
[`compile/special-rules`](../specials/compile-special-rules.md) compiles.

**Behaviour:** The header carries a literals list (`[]` or `[x y]`), and
may carry a name before it. Each rule's pattern column is
`[COMPILER PATTERN]`: the first subform names the compiler parameter the
rule body receives, the second is the rule pattern. A rule body of
several forms is wrapped in `begin`. Does not accept peers.

A rule block header is either `case FORM:`, where `case` is followed by
exactly one form and that form is the pattern, or a header whose first
element is a symbol, in which case the whole header is the pattern. A
header that is a lone parenthesized form, `([c (foo x)]):`, is rejected
with `rule must be a block`; write `case [c (foo x)]:` instead.

**Named header:** `special-rules NAME [LIT ...]:` puts NAME in the emitted
form, which is where the transformer takes its identity for help text and
match errors. `syntax` and `extend-syntax` supply that name
themselves, so the named header belongs to a transformer bound directly,
as `var compile/NAME:`.

**Syntax:**

```
special-rules [LIT ...]:
  case [COMPILER PATTERN]:
    BODY...

special-rules NAME [LIT ...]:
  COMPILER PATTERN:
    BODY...
```

**Examples:**

```koskari
syntax say-hi:
  special-rules []:
    case [c (say-hi thing)]:
      var t thing.datum
      `(print "hi" ,t)

(say-hi "there")
;; prints: hi there
```

```koskari
var compile/say-hi:
  special-rules say-hi []:
    c (say-hi thing):
      var t thing.datum
      `(print "hi" ,t)
;; -> (var compile/say-hi
;;      (special-rules say-hi [] ((c (say-hi thing)) (begin ...))))
```
