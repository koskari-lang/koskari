# layout/class
**Symbol:** `layout/class`

**Use-case:** Class **definition** block syntax. Lowers to
`(class NAME SUPERS (begin BODY))`, which the
[`class`](../macros/compile-class.md) macro expands to
`(var NAME (build-class NAME SUPERS (begin BODY)))`: the block builds the
class and binds it to **NAME** in the scope the block appears in. For a
class value that binds nothing, use
[`build-class NAME:`](layout-build-class.md).

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(class NAME)` or `(class NAME SUPERS)`.
When supers are omitted, uses nil so the result is `(class NAME () BODY)`.
When present, SUPERS is the parsed form (e.g. a single base or a parenthesized
list). Raises LayoutError if the header has fewer than two elements (missing
name) or more than three (extra forms after supers).

A `global` prefix, `global class NAME SUPERS:`, binds **NAME** at the
module even from inside a function body; see
[compile/global](../macros/compile-global.md). Decorator lines above
the header decorate the built class before it is bound; see
[`decorate`](../macros/compile-decorate.md).

**Syntax:**

```
class NAME:
  BODY
;; -> (class NAME () (begin BODY))

class NAME (Base1 Base2):
  BODY
;; -> (class NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```koskari
class C:
  var x 1
;; -> (class C () (begin (var x 1)))

class C (Base):
  var x 1
;; -> (class C (Base) (begin (var x 1)))

import-from dataclasses dataclass

@dataclass
class Point:
  var x :: int 0
  var y :: int 0
;; -> (decorate (dataclass) (class Point () (begin (var x :: int 0) (var y :: int 0))))

def setup []:
  global class Registry []:
    var size 0
;; Registry is bound at the module once setup runs
```

**Also:** macro [`class`](../macros/compile-class.md).
