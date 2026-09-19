# Point class

A `@dataclass` Koskari class with typed fields, a generator method that
yields neighbor points using `each` and `itertools.product`, an in-place
`move` method, and a `coord` property with a setter. The same program is
`examples/dataclass.ki` in the repository.

## The program

```koskari
import-from dataclasses dataclass
import-from itertools product


define around [i reach: 1]:
  (range (- i reach) (+ i reach 1))


@dataclass
define-class Point []:
  """ A point in 2D space """

  define x :: int 0
  define y :: int 0

  define neighbors [self reach: 1]:
    each [[x y] (product (around self.x reach) (around self.y reach))]:
      (Point x y)

  define move [self x: 0 y: 0]:
    (incr! self.x x)
    (incr! self.y y)

  @property
  define coord [self]:
    #(self.x self.y)

  @coord.setter
  define coord [self value]:
    define-values [x y] value
    (set! self.x x)
    (set! self.y y)


when-main:
  print "Point.__annotations__:" Point.__annotations__

  define p (Point 3 5)
  print "p:" p
  print "p.coord:" p.coord

  print "p.neighbors:"
  for-each [n (p.neighbors)]:
    print "  " n

  set! p.coord #(10 20)
  print "p after set! p.coord:" p

  (p.move x: 1)
  print "p after move:" p
```

`define x :: int 0` is an annotated field, which is what `@dataclass`
reads to build `__init__` and `__repr__`. `neighbors` uses `each`, so it
returns a generator.

## Run

Save as `point.ki` and:

```bash
koskari point.ki
```

Expected:

```text
Point.__annotations__: {'x': <class 'int'>, 'y': <class 'int'>}
p: Point(x=3, y=5)
p.coord: (3, 5)
p.neighbors:
   Point(x=2, y=4)
   Point(x=2, y=5)
   Point(x=2, y=6)
   Point(x=3, y=4)
   Point(x=3, y=5)
   Point(x=3, y=6)
   Point(x=4, y=4)
   Point(x=4, y=5)
   Point(x=4, y=6)
p after set! p.coord: Point(x=10, y=20)
p after move: Point(x=11, y=20)
```

## See also

- [layout/define-class](../../../reference/layouts/layout-define-class.md)
- [compile/each](../../../reference/macros/compile-each.md) (generator comprehension macro)
- [Property accessors](property-accessors.md)
- [From Python: Interop](../interop.md)
