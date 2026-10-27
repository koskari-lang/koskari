# compile/class

**Syntax:** `(class NAME SUPERS BODY ...)`

**Behaviour:** Expands to `(var NAME (build-class NAME SUPERS BODY ...))`,
building a class and binding it to **NAME** in the scope the form
appears in, as [`var`](../specials/compile-var.md) binds. At the top of
a module **NAME** is a module global; inside a function body it is a
local of that function. To bind at the module from inside a body, write
`global class`; see [compile/global](compile-global.md).

**SUPERS** is the base list, empty for no explicit base, with an
optional `meta: Metaclass` keyword. **BODY** runs in the class
namespace: [`var`](../specials/compile-var.md) for class attributes,
[`def`](compile-def.md) for methods, and a leading string for the class
`__doc__`. The class itself is built by
[`build-class`](../specials/compile-build-class.md), which documents the
namespace, `__qualname__`, `__module__` and closure rules.

`class` binds only **NAME**. It defines no instance predicate; test
membership with [`instance?`](../specials/compile-instance.md):
`(instance? x Point)`.

The block form [`class NAME SUPERS:`](../layouts/layout-class.md) wraps
the body in a `begin`, and SUPERS may be omitted there. A decorator line
above `class` decorates the built class and binds the result; see
[`decorate`](compile-decorate.md).

**Examples:**

```koskari
class Point []:
  var x 0
  var y 0

  def coord [self]:
    #(self.x self.y)

(.coord (Point))          ;; (0, 0)
(instance? (Point) Point) ;; True
(instance? 3 Point)       ;; False
```

The s-expression form:

```koskari
(class Point [] (var x 0))
;; -> (var Point (build-class Point [] (var x 0)))
```

Subclassing and `super`:

```koskari
class Base []:
  def __init__ [self]:
    (set-attr self ready True)

class Child (Base):
  def __init__ [self]:
    (.__init__ (super))
    (set-attr self extra 1)
```

A dataclass. Import `dataclass` from Python and place it above the
class; it reads the `::` annotations on the fields and generates
`__init__`, `__repr__` and `__eq__`:

```koskari
import-from dataclasses dataclass

@dataclass
class Point:
  var x :: int 0
  var y :: int 0

(Point 1 2)                 ;; Point(x=1, y=2)
(== (Point 1 2) (Point 1 2)) ;; True
```

Options pass through as a call, as in Python:
`@(dataclass frozen: True order: True)`.

**See also:** [`build-class`](../specials/compile-build-class.md),
[layout/class](../layouts/layout-class.md),
[`super`](compile-super.md),
[Classes and objects](../../tutorials/from-python/classes-and-objects.md).

**Also:** layout handler [`class`](../layouts/layout-class.md).
