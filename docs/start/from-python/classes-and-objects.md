# Classes and objects

Koskari classes compile to ordinary Python types. You can call methods from
Python, attach Python decorators such as `@dataclass`, and use attribute syntax
`obj.field` in method bodies when the reader allows it.

---

## What this chapter builds

You will declare a class with `define-class`, add fields and methods with
layout bodies, apply `@dataclass`, and run a small program that constructs
instances and calls methods.

---

## Defining a class

> **Q:** How do I declare a class (the equivalent of `class Point:` in Python)?
>
> **A:** Use **`define-class`** with a layout body — same colon-and-indent shape
> as `define` for functions:
>
> ```koskari
> define-class Point []:
>   """
>   A point in 2D space
>   """
>   define x :: int 0
>   define y :: int 0
> ```
>
> The `[]` after the class name is the base list. Empty bases mean no explicit
> superclass, like `class Point:` in Python. To subclass:
>
> ```koskari
> define-class Child (Parent):
>   ...
> ```
>
> See [layout/define-class](../../reference/layouts/layout-define-class.md).

> **Q:** How do I add instance fields with a type and a default value?
>
> **A:** Use **`define`** inside the class body, with `::` for the type and a
> trailing default expression:
>
> ```koskari
> define x :: int 0
> define y :: int 0
> ```
>
> That mirrors annotated attributes in Python 3 (`x: int = 0`). The annotations
> land on the class `__annotations__` as real objects (`{'x': int, 'y': int}`);
> at runtime instances behave like normal Python objects.

---

## Methods

> **Q:** How do I define a method on the class?
>
> **A:** Another **`define`** inside the class body. Put **`self`** first in the
> formals list, then any other parameters (defaults and keywords work like on
> module-level functions):
>
> ```koskari
> define get_coord [self]:
>   #(self.x self.y)
> ```
>
> The last expression in the body is the return value unless you use **`return`**
> for an early exit.

> **Q:** Can I write `self.x` in a method, or only `(attr self x)`?
>
> **A:** Use **`self.x`** (and method calls like `self.set_x val`) in class
> bodies — that is the usual style. Forms such as `(attr self x)` and
> `(set-attr self real_x v)` remain available when the name is not a simple
> dotted identifier (for example a slot named `real_x`).

> **Q:** How do I write `__init__`?
>
> **A:** Define it like any other method; Koskari emits a normal Python class,
> so dunder names follow Python rules:
>
> ```koskari
> define __init__ [self x]:
>   (set! self.x x)
> ```

---

## Property accessors

> **Q:** How do I write a `@property` getter and setter?
>
> **A:** Use `@property` above a `define` inside the class body, then chain
> `.setter` (or `.deleter`) the same way Python does:
>
> ```koskari
> define-class Circle []:
>   define _radius :: float 0.0
>
>   @property
>   define radius [self]:
>     self._radius
>
>   @radius.setter
>   define radius [self value]:
>     (set! self._radius (float value))
> ```
>
> At the call site, attribute access and assignment work normally:
>
> ```koskari
> define c (Circle)
> (set! c.radius 3.0)
> print c.radius   ; → 3.0
> ```

> **Q:** Do I need to add `@demand` to property accessors the way I do for
> ordinary methods?
>
> **A:** No. Koskari replaces Python's built-in `property` with **`KosProperty`**
> in the bootstrap, which automatically wraps any Koskari function accessor in
> `Demand` at registration time — constructor, `.setter`, and `.deleter` all
> apply the wrap. You write plain `define` inside the class body; the demand
> path is wired up for you.
>
> Plain Python callables (for example a `lambda` or a Python `def`) are stored
> unchanged, so mixing Koskari and Python accessors on the same descriptor works
> as expected.

> **Q:** What about `.deleter`?
>
> **A:** Same pattern — chain `.deleter` on the property name:
>
> ```koskari
> @radius.deleter
> define radius [self]:
>   (set! self._radius 0.0)
> ```

See [Property accessors recipe](../../patterns/recipes/property-accessors.md)
for a complete runnable example.

---

## Python decorators on classes

> **Q:** Can I put Python’s `@dataclass` on a Koskari class?
>
> **A:** Yes. Stack `@` lines above **`define-class`**, the same as above `class`
> in Python:
>
> ```koskari
> import-from dataclasses dataclass

> @dataclass
> define-class Point []:
>   define x :: int 0
>   define y :: int 0
> ```
>
> Construction uses normal call syntax: `(Point 3 5)` → Python `Point(3, 5)`.
> Instances expose `.x`, `.y`, and `__annotations__` like a Python dataclass.

> **Q:** Do other Python decorators (`@functools…`, custom wrappers) work on
> classes and methods?
>
> **A:** Any importable decorator callable works: `@` above `define-class` or
> `define`. You use them the same way as in Python; no extra Koskari-specific
> ceremony.

---

## Types and instances from Python

> **Q:** After `define-class`, is `Point` a real Python class I can import and
> instantiate from `.py` code?
>
> **A:** Yes — it is a normal Python `type`. Load the `.ki` module (Koskari
> importer enabled) and use `Point` like any other class.

> **Q:** How do I test whether a value is an instance of my class?
>
> **A:** Use Python’s **`isinstance`**. Koskari also defines a predicate per
> class (for example **`Point?`**) that you can call from Koskari code.

---

## Runnable mini-program

Save `point.ki`:

```koskari
import-from dataclasses dataclass

@dataclass
define-class Point []:
  define x :: int 0
  define y :: int 0

  define label [self]:
    (+ "Point at (" (str self.x) ", " (str self.y) ")")

when-main:
  define p (Point 1 2)
  print (p.label)
```

Expected: `Point at (1, 2)`

A larger `Point` with `neighbors` and `@dataclass`: [Point class](../../patterns/recipes/point-class.md).

---

## Try it

Define a small class in a file, run it with `koskari`, then import that module
from Python in the same environment if you need to call it from `.py` code.

---

## Remember

`define-class` + indented `define` methods replace `class` / `def` for most
OOP. Use `@dataclass` and other Python decorators as in Python. Instances are
Python objects; attribute access and methods interoperate both ways.

Next: [Programs and modules](programs-and-modules.md).
