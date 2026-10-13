# layout/define-class
**Symbol:** `layout/define-class`

**Use-case:** Class **definition** block syntax: lowers to
`(define-class NAME SUPERS (begin BODY))`, which the
**`compile/define-class`** macro expands to **`var`** plus **`class`** (see
[`define-class`](../macros/compile-define-class.md)). Same header shape as
**`layout/class`**, but the read result binds **NAME** to the class after
expansion.

**Behaviour:** No peers. Parses header as `(define-class NAME)` or
`(define-class NAME SUPERS)`. When supers are omitted, uses nil. Raises
LayoutError if the header has fewer than two elements (missing name) or more
than three.

**Syntax:**

```
define-class NAME:
  BODY
;; -> (define-class NAME () (begin BODY))

define-class NAME (Base1 Base2):
  BODY
;; -> (define-class NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```koskari
define-class C:
  x
;; -> (define-class C () (begin x))

define-class C (Base):
  x
;; -> (define-class C (Base) (begin x))
```
