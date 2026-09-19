# Point class

A `@dataclass` Koskari class with typed fields, a generator method that
yields neighbor points using `each` and `itertools.product`, an in-place
`move` method, and a `coord` property with a setter. The same program is
`examples/dataclass.ki` in the repository.

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
