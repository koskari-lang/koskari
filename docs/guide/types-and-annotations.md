# Types and annotations

Values are Python objects with Python types. Annotations are metadata
that tools and readers use; nothing checks them at a call.

## Writing an annotation

`::` attaches an annotation to a definition, a formal, or a return:

```koskari
define total :: int 0

define double :: int [(n :: int)]:
  (* n 2)

define-class Point []:
  define x :: int 0
  define y :: int 0
```

An annotated formal is parenthesized inside the formals list:
`[(n :: int)]`. The parentheses group the name with its annotation, so
the formals list still reads as one parameter. A bare `[n :: int]` is a
compile error that names the parenthesized spelling.

The annotation is an ordinary expression evaluated in the defining
scope. A string annotation, `:: "Request"`, stays a lazily resolved
forward reference as it does in Python.

## Where it lands

A function's annotations go to `__annotations__` and `inspect.signature`
reports them:

```koskari
import inspect
(print double.__annotations__)        ;; {'n': <class 'int'>, 'return': <class 'int'>}
(print (inspect.signature double))    ;; (n: int) -> int
```

A class's annotated `define` forms go to the class `__annotations__`,
which is what `dataclasses.dataclass` reads to generate `__init__`,
`__repr__`, and `__eq__`. Any Python tool that consumes annotations,
pydantic, FastAPI, `typing.get_type_hints`, sees the same objects it
would see from a Python definition.

## Checking at runtime

An annotation does not check anything. A function that wants a guarantee
checks it:

```koskari
define take [count]:
  when (< count 0):
    raise (ValueError "count must be non-negative")
  ...
```

`instance?` and `subclass?` are the type tests, and a class name with a
`?` suffix is bound as a predicate for classes defined with a bare
`define-class`.

## Contracts by convention

A public function states its expectations in three places: its name,
its parameter names and annotations, and an early check with a clear
error where a bad value would otherwise fail somewhere confusing later.
Annotations do the second job well and the third not at all.
