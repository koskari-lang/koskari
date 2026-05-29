# layout/class
**Symbol:** `layout/class`

**Use-case:** Class **expression** block syntax. Lowers to `(compile/class NAME SUPERS
(compile/begin BODY))`. The form evaluates to the new class object; it does **not**
by itself bind **NAME** in the module (see SPECIALS.md §
compile/class). To define a named class in one step, use **`layout/define-class`**
or wrap with **`compile/define`**.

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(class NAME)` or `(class NAME SUPERS)`.
When supers are omitted, uses nil so the result is `(compile/class NAME () BODY)`.
When present, SUPERS is the parsed form (e.g. a single base or a parenthesized
list). Raises LayoutError if the header has fewer than two elements (missing
name) or more than three (extra forms after supers).

**Syntax:**

```
class NAME:
  BODY
;; → (compile/class NAME () (compile/begin BODY))

class NAME (Base1 Base2):
  BODY
;; → (compile/class NAME (Base1 Base2) (compile/begin BODY))
```

**Examples:**

```scheme
class C:
  x
;; → (compile/class C () (compile/begin x))

class C (Base):
  x
;; → (compile/class C (Base) (compile/begin x))

class C ():
  x
;; → (compile/class C () (compile/begin x))
```

**Implementation:** `koskari/reader/layouts/classes.layout_class`

