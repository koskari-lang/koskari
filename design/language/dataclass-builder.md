# A dataclass builder in Koskari

Status: proposed. Recorded 2026-10-02.

## Why

A dataclass is the stdlib decorator applied to a `class`:

```koskari
import-from dataclasses dataclass

@dataclass
class Point []:
  var x :: int 0
  var y :: int 0
```

`dataclasses.dataclass` generates `__init__`, `__repr__`, `__eq__` and
the rest by writing Python source and running `exec` against
`sys.modules[cls.__module__].__dict__`. For a Koskari module that dict
is the module namespace, which works only because the namespace is a
dict subclass whose reads relay to the module's bindings
([archive/module-dict.md](../archive/module-dict.md)). Koskari's
own dataclasses should not depend on that relay, or on `exec` at all:
the methods are ordinary functions over the field list and the compiler
is already in hand at expansion time.

## Shape

A `dataclass` builtin written in Koskari, in the bootstrap beside
the `class` macro, taking the class and the stdlib decorator's options
(`init`, `repr`, `eq`, `order`, `unsafe_hash`, `frozen`, `match_args`,
`kw_only`, `slots`). `@dataclass` over `class` resolves to it without
the import.

1. **Collect the fields.** Walk the class's `__annotations__` in order,
   as the stdlib does: each annotated name is a field; its class
   attribute is the default, a `dataclasses.field()` result is a field
   spec, `ClassVar` and `InitVar` annotations are recognised the stdlib
   way, and `KW_ONLY` splits the positional run. Inherited fields come
   from each base's `__dataclass_fields__`, so a Koskari dataclass can
   extend a Python one and the reverse.
2. **Record the metadata the stdlib reads.** Set `__dataclass_fields__`
   to real `dataclasses.Field` instances and `__dataclass_params__` to a
   `_DataclassParams`, so `dataclasses.fields`, `asdict`, `astuple`,
   `replace`, `is_dataclass` and `__match_args__` consumers see a
   dataclass with no idea who built it.
3. **Generate the methods as Koskari functions.** `__init__` assigns
   each field from its parameter, applies defaults and default
   factories, and runs `__post_init__` with the `InitVar`s; `__repr__`
   joins `name=value!r` in field order; `__eq__` compares the field
   tuples when the classes match; `order` adds the four comparisons;
   `unsafe_hash` and `frozen` decide `__hash__`; `frozen` installs
   `__setattr__` and `__delattr__` that raise `FrozenInstanceError`;
   `slots` rebuilds the class with `__slots__`. Each method is a
   quasiquoted `lambda` over the field list compiled in the defining
   module through `compile`, so it closes over that module's bindings
   as any method there does, and no Python source is written.
4. **Keep the stdlib's errors.** A field without a default after one
   with a default, a mutable default, a `KW_ONLY` given twice, and
   `order` without `eq` raise the same `TypeError` and `ValueError`
   messages, so a user moving a class between the two builders sees
   the same complaints.

## What stays with the stdlib

`field`, `fields`, `asdict`, `astuple`, `replace`, `is_dataclass`,
`MISSING`, `KW_ONLY`, `InitVar`, `FrozenInstanceError`: all of them
work from the metadata in step 2. A Python module applying the stdlib
decorator to a Koskari class still takes the `exec` path through the
relay; that is the stdlib's business, and the relay keeps it working.

## Open

- Whether `slots=True` rebuilds through `class` the way the stdlib
  rebuilds the type, or is refused in the first version.
- `__match_args__` is a tuple of field names; the Koskari match
  patterns that read it ([MATCHER.md](../MATCHER.md)) get it for free.
- Default-factory fields need the factory called per instance; the
  generated `__init__` holds the factory in a closure rather than a
  `_dflt_` local as the stdlib does.
