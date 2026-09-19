# Property accessors

A Koskari class with `@property` getter, `.setter`, and `.deleter` accessors.
Koskari's `KosProperty` automatically wraps Koskari function accessors in the
demand path — no `@demand` boilerplate required.

## Prerequisites

- [Quickstart](../../../quickstart.md)
- [From Python: Classes and objects](../classes-and-objects.md)

## The demand-path difference

In plain Python, writing a coroutine-backed property accessor requires
explicit demand wrapping:

```python
# Python — manual wrapping needed
prop = property(demand(kos_getter), demand(kos_setter))
```

With `KosProperty` (Koskari's built-in `property`), any Koskari function
registered as `fget`, `fset`, or `fdel` is wrapped automatically. You write
ordinary `@property` syntax; the demand path is wired for you:

```koskari
@property
define radius [self]:      ; stored as Demand(radius) automatically
  self._radius
```

Plain Python callables pass through unchanged, so mixed accessors work.

## Core shape

```koskari
@dataclass
define-class Circle []:
  """ A circle with a validated radius. """

  define _radius :: float 0.0

  @property
  define radius [self]:
    self._radius

  @radius.setter
  define radius [self value]:
    when (< value 0):
      raise (ValueError "radius must be non-negative")
    (set! self._radius (float value))

  @radius.deleter
  define radius [self]:
    (set! self._radius 0.0)

  define area [self]:
    (* 3.14159 self._radius self._radius)


when-main:
  define c (Circle)
  (set! c.radius 5.0)
  print "radius:" c.radius
  print "area:"   (c.area)

  del! c.radius
  print "after del, radius:" c.radius
```

## Try it

Save as `circle.ki` and run:

```bash
koskari circle.ki
```

Expected output:

```
radius: 5.0
area: 78.53975
after del, radius: 0.0
```

The [Point class](point-class.md) example puts a property with a setter on
a `@dataclass` class.

## See also

- [From Python: Classes and objects — Property accessors](../classes-and-objects.md#properties)
- [layout/define-class](../../../reference/layouts/layout-define-class.md)
- [compile/decorate](../../../reference/macros/compile-decorate.md)
