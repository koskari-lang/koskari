# Classes and objects

A Koskari class is a Python class. Methods are callable from Python,
Python decorators apply, and instances are ordinary objects.

## Defining

:::{pair}
```python
class Point:
    """A point in 2D space"""
    x: int = 0
    y: int = 0
```
+++
```koskari
define-class Point []:
  """ A point in 2D space """
  define x :: int 0
  define y :: int 0
```
:::

:::{dialogue}
What is the `[]`?
+++
The base list. Empty means no explicit base, as `class Point:` does.
`define-class Child (Parent):` subclasses. The `::` carries an
annotation, and it lands in `__annotations__` as the real type object.
:::

## Methods

:::{pair}
```python
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def coord(self):
        return (self.x, self.y)
```
+++
```koskari
define-class Point []:
  define __init__ [self x y]:
    set! self.x x
    set! self.y y

  define coord [self]:
    #(self.x self.y)
```
:::

:::{dialogue}
Is `self` special?
+++
No more than in Python. It is the first formal, by convention named
`self`. `self.x` reads the attribute and `set! self.x` writes it; dunder
methods follow Python's rules because the class is a Python type.
:::

## Properties

:::{pair}
```python
class Circle:
    _radius = 0.0

    @property
    def radius(self):
        return self._radius

    @radius.setter
    def radius(self, value):
        self._radius = float(value)
```
+++
```koskari
define-class Circle []:
  define _radius :: float 0.0

  @property
  define radius [self]:
    self._radius

  @radius.setter
  define radius [self value]:
    set! self._radius (float value)
```
:::

:::{dialogue}
Does `property` need `@demand`?
+++
No. Koskari's `property` wraps a Koskari accessor for the demand path
itself, so a getter, setter, and deleter are plain `define` forms.
:::

## Python decorators

:::{pair}
```python
from dataclasses import dataclass

@dataclass
class Point:
    x: int = 0
    y: int = 0

p = Point(3, 5)
```
+++
```koskari
import-from dataclasses dataclass

@dataclass
define-class Point []:
  define x :: int 0
  define y :: int 0

define p (Point 3 5)
```
:::

:::{dialogue}
Which decorators work?
+++
Any importable callable, above `define-class` or above a method. The
class body's annotated `define` forms are what `dataclass` reads, so it
generates `__init__`, `__repr__`, and `__eq__` as it would for the
Python spelling.
:::

## Instances

:::{pair}
```python
p = Point(3, 5)
isinstance(p, Point)
p.x
```
+++
```koskari
define p (Point 3 5)
(instance? p Point)
p.x
```
:::

:::{dialogue}
Can Python code use the class?
+++
Yes. Import the `.ki` module from Python and `Point` is a normal type,
with `isinstance`, `dataclasses.fields`, and everything else that works
on one.
:::

## Run it

Save as `point.ki`:

```koskari
import-from dataclasses dataclass

@dataclass
define-class Point []:
  define x :: int 0
  define y :: int 0

  define label [self]:
    f"Point at ({self.x}, {self.y})"

@entry-point
define main []:
  define p (Point 1 2)
  print (p.label)
  print p
```

```text
Point at (1, 2)
Point(x=1, y=2)
```

A larger version with a generator method and a property is the
[Point class](examples/point-class.md) example.
