# compile/#str
**Symbol:** `compile/#str`

**Syntax:** `(#str OBJ ...)`

**Use-case:** String conversion through `__str__`, variadic so that the parts
of an f-string concatenate in one form. `(#str)` with no arguments is the empty
string.

**Behaviour:** Emits `OBJECT_STR`, reducing left to right over the operands.
Leading constant operands fold at compile time; see
[Object operator specials](object-operators/index.md) for the shared folding
rules.

**Examples:**

```koskari
>>> (#str 42)
"42"
>>> (#str 1 2 3)
"123"
>>> (#str)
""
```

An f-string reads to this form, so `f"a{x}b"` compiles as a `#str` over its
parts.

**Also:** function [`#str`](../runtime/functions.md#collection-builders).
