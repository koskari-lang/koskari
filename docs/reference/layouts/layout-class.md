# layout/class
**Symbol:** `layout/class`

**Use-case:** Class **expression** block syntax. Lowers to `(class NAME SUPERS
(begin BODY))`. The form evaluates to the new class object; it does **not**
by itself bind **NAME** in the module (see [compile/class](../specials/compile-class.md)).
To define a named class in one step, use **`layout/define-class`**
or wrap with **`compile/var`**.

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(class NAME)` or `(class NAME SUPERS)`.
When supers are omitted, uses nil so the result is `(class NAME () BODY)`.
When present, SUPERS is the parsed form (e.g. a single base or a parenthesized
list). Raises LayoutError if the header has fewer than two elements (missing
name) or more than three (extra forms after supers).

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
  x
;; -> (class C () (begin x))

class C (Base):
  x
;; -> (class C (Base) (begin x))

class C ():
  x
;; -> (class C () (begin x))
```
