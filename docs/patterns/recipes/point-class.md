# Point class

A `@dataclass` Koskari class with typed fields and a generator method that
yields neighbor points using `each` and `itertools.product`.

## Prerequisites

- [Install and run](../../start/install-run.md)
- [From Python: Classes and objects](../../start/from-python/classes-and-objects.md)

## Source

Repository file: [examples/dataclass.ki](../../../examples/dataclass.ki)

## Run

```bash
koskari examples/dataclass.ki
```

Expected output includes `Point` metadata, `Point(3, 5)`, and a list of neighbor
coordinates printed under `p.neighbors:`.

## Core shape

```koskari
import-from dataclasses dataclass
import-from itertools product

@dataclass
define-class Point []:
  define x :: int 0
  define y :: int 0

  define neighbors [self reach: 1]:
    each [[x y] (product (around self.x reach) (around self.y reach))]:
      (Point x y)

when-main:
  define p (Point 3 5)
  ...
```

## See also

- [layout/define-class](../../reference/layouts/layout-define-class.md)
- [compile/each](../../reference/macros/index.md) (generator comprehension macro)
- [From Python: Interop](../../start/from-python/interop.md)
