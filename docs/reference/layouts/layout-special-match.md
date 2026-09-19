# layout/special-match
**Symbol:** `layout/special-match`

**Use-case:** Block syntax for the `special-match` transformer, normally
the inner block of a `define-syntax` or `extend-syntax` block. No
literals list; each rule carries an explicit match-style pattern.
Converts indented rule blocks into the
`(special-match [NAME] (PATTERN BODY) ...)` form that
[`compile/special-match`](../specials/compile-special-match.md) compiles.

**Behaviour:** Same rule shape as
[`layout/special-rules`](layout-special-rules.md), with no literals list
in the header: each rule's pattern column is `[COMPILER PATTERN]`, the
first subform naming the compiler parameter the rule body receives and
the second being the explicit pattern. A rule body of several forms is
wrapped in `begin`. Does not accept peers.

A rule block header is either `case FORM:`, where `case` is followed by
exactly one form and that form is the pattern, or a header whose first
element is a symbol, in which case the whole header is the pattern. A
header that is a lone parenthesized form is rejected with
`rule must be a block`.

**Named header:** `special-match NAME:` puts NAME in the emitted form,
which is where the transformer takes its identity for help text and
match errors. `define-syntax` and `extend-syntax` supply that name
themselves, so the named header belongs to a transformer bound directly,
as `define compile/NAME:`.

**Syntax:**

```
special-match:
  case [COMPILER PATTERN]:
    BODY...

special-match NAME:
  case [COMPILER PATTERN]:
    BODY...
```

**Examples:**

```koskari
define-syntax say-bye:
  special-match:
    case [c (say-bye (BIND thing))]:
      define t thing.datum
      `(print "bye" ,t)

(say-bye "now")
;; prints: bye now
```

```koskari
define compile/say-bye:
  special-match say-bye:
    case [c (say-bye (BIND thing))]:
      define t thing.datum
      `(print "bye" ,t)

(say-bye "now")
;; prints: bye now
```
