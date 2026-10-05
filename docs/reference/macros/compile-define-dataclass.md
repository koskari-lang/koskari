# compile/define-dataclass

**Syntax:** `(define-dataclass NAME SUPERS BODY ...)`

**Behaviour:** Imports `dataclass` and `field` from `dataclasses`,
defines the class with `@dataclass` applied, and binds `NAME?` as the
instance predicate. The body holds annotated `define` forms for the
fields, as a `define-class` body does; `dataclass` reads the
annotations and generates `__init__`, `__repr__`, and `__eq__`.

**Examples:**

```koskari
define-dataclass Point:
  define x :: int 0
  define y :: int 0

(Point 1 2)        ;; Point(x=1, y=2)
(Point? (Point))   ;; True

define-dataclass Point3 [Point]:
  define z :: int 0
```

In block form the supers may be omitted, as with `define-class`; see
[`layout/define-dataclass`](../layouts/layout-define-dataclass.md). A
subclass that adds no fields is `(define-dataclass Child [Parent])`.

```koskari
(define-dataclass P [] (define x :: int 0) (define y :: int 0))
```

**See also:** [`define-class`](compile-define-class.md), which takes
`@dataclass` from Python directly and any of its options.
