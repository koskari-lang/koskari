# layout/build-class
**Symbol:** `layout/build-class`

**Use-case:** Class **expression** block syntax. Lowers to
`(build-class NAME SUPERS (begin BODY))`. The form evaluates to the new
class object and does **not** bind **NAME** (see
[compile/build-class](../specials/compile-build-class.md)). To bind it,
store the value with `var`, or write `global class` to bind it at the
module.

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(build-class NAME)` or
`(build-class NAME SUPERS)`. When supers are omitted, uses nil so the
result is `(build-class NAME () BODY)`. When present, SUPERS is the
parsed form (e.g. a single base or a parenthesized list). Raises
LayoutError if the header has fewer than two elements (missing name) or
more than three (extra forms after supers).

**Syntax:**

```
build-class NAME:
  BODY
;; -> (build-class NAME () (begin BODY))

build-class NAME (Base1 Base2):
  BODY
;; -> (build-class NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```koskari
build-class C:
  x
;; -> (build-class C () (begin x))

build-class C (Base):
  x
;; -> (build-class C (Base) (begin x))

var Point:
  build-class Point:
    var x 0
    var y 0
;; -> (var Point (build-class Point () (begin (var x 0) (var y 0))))
```
