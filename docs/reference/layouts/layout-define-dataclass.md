# layout/define-dataclass
**Symbol:** `layout/define-dataclass`

**Use-case:** Dataclass **definition** block syntax: lowers to
`(define-dataclass NAME SUPERS (begin BODY))`, which the
**`compile/define-dataclass`** macro expands to a `@dataclass` class
bound to **NAME** plus the **`NAME?`** predicate (see
[`define-dataclass`](../macros/compile-define-dataclass.md)). Same
header shape as **`layout/define-class`**.

**Behaviour:** No peers. Parses header as `(define-dataclass NAME)` or
`(define-dataclass NAME SUPERS)`. When supers are omitted, uses nil.
Raises LayoutError if the header has fewer than two elements (missing
name) or more than three.

**Syntax:**

```
define-dataclass NAME:
  BODY
;; -> (define-dataclass NAME () (begin BODY))

define-dataclass NAME (Base1 Base2):
  BODY
;; -> (define-dataclass NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```koskari
define-dataclass Point:
  var x :: int 0
  var y :: int 0
;; -> (define-dataclass Point () (begin (var x :: int 0) (var y :: int 0)))

define-dataclass Point3 [Point]:
  var z :: int 0
;; -> (define-dataclass Point3 [Point] (begin (var z :: int 0)))
```

**See also:** [`define-class`](layout-define-class.md), the same header
for a plain class.
