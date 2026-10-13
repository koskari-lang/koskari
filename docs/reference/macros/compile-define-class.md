# compile/define-class

**Syntax:** `(define-class NAME SUPERS BODY ...)`

**Behaviour:** Expands to `(var NAME (class NAME SUPERS BODY ...))`
and binds `NAME?` to `(lambda [value] (instance? value NAME))`. `SUPERS`
is the base list, empty for no explicit base. The layout form
[`define-class NAME SUPERS:`](../layouts/layout-define-class.md) wraps
the body in a `begin`. Under a decorator the name is hoisted by
[`decorate`](compile-decorate.md), so the predicate is bound only for an
undecorated definition.

**Examples:**

```koskari
define-class Point []:
  var x :: int 0
  var y :: int 0

  def coord [self]:
    #(self.x self.y)

(Point? (Point))     ;; True
(Point? 3)           ;; False
```

```koskari
(define-class Point [] (var x 0))
```

**See also:** [`class`](../specials/compile-class.md),
[`define-dataclass`](compile-define-dataclass.md),
[Classes and objects](../../tutorials/from-python/classes-and-objects.md).
