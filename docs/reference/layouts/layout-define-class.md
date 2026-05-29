# layout/define-class

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/define-class`

**Use-case:** Class **definition** block syntax: lowers to
`(compile/define-class NAME SUPERS (compile/begin BODY))`, which the
**`compile/define-class`** macro expands to **`define`** plus **`class`** (see
[MACROS.md](../../design/MACROS.md) § compile/define-class). Same header shape as
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
;; → (compile/define-class NAME () (compile/begin BODY))

define-class NAME (Base1 Base2):
  BODY
;; → (compile/define-class NAME (Base1 Base2) (compile/begin BODY))
```

**Examples:**

```scheme
define-class C:
  x
;; → (compile/define-class C () (compile/begin x))

define-class C (Base):
  x
;; → (compile/define-class C (Base) (compile/begin x))
```

**Implementation:** `koskari/reader/layouts/classes.layout_define_class`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
