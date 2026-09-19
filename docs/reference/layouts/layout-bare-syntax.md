# layout/bare-syntax
**Symbol:** `layout/bare-syntax`

**Use-case:** Block syntax for the bare arm of a transformer: the
expansion that applies when the transformer's name is used as a lone atom
rather than at the head of a call form. Normally the inner block of a
`define-syntax` or
[`extend-syntax`](../macros/compile-extend-syntax.md) block. A
transformer has at most one bare arm.

**Behaviour:** The header names the called-by slot, and may carry a name
before it. The body is the template, left unexpanded the way a
`syntax-rules` template is; a body of several forms is wrapped in
`begin`. Produces `(bare-syntax SLOT TEMPLATE)`, or
`(bare-syntax NAME SLOT TEMPLATE)` with a name in the header. SLOT is the
only binding the template may refer to, and it captures the symbol the
expansion was reached through. Does not accept peers. A header with no
symbol raises `bare-syntax requires a called-by symbol`, and an empty body
raises `bare-syntax requires a template`.

**Named header:** `bare-syntax NAME SLOT:` puts NAME in the emitted form,
which is where the transformer takes its identity. `define-syntax` and
`extend-syntax` supply that name themselves, so the named header belongs
to a transformer bound directly, as `define compile/NAME:`.

**Syntax:**

```
bare-syntax SLOT:
  TEMPLATE...

bare-syntax NAME SLOT:
  TEMPLATE...
```

**Examples:**

```koskari
define-syntax marker:
  syntax-rules []:
    case (marker x):
      (print "called" x)

extend-syntax marker:
  bare-syntax slot:
    "bare marker"

(marker 1)
(print marker)
;; prints: called 1
;; prints: bare marker
```

```koskari
define compile/marker:
  bare-syntax marker slot:
    "the marker"

(print marker)
;; prints: the marker
;; -> (define compile/marker (bare-syntax marker slot "the marker"))
```
