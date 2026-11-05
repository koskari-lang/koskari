# compile/default! and compile/unassigned?

**Syntax:** `(unassigned? LOCATION)`, `(default! LOCATION VALUE)`

**Behaviour:** `unassigned?` is true when the storage location currently
holds no value, and false otherwise. It accepts the target shapes
[`ref`](compile-ref.md) accepts. A declared-but-unstored binding is
unassigned; so is an attribute or subscript that does not exist.

`default!` stores VALUE into LOCATION unless the location is already assigned
to a value of its own that is truthy. It behaves like

```koskari
unless (and (ref LOCATION) LOCATION):
  set! LOCATION VALUE
```

so a location holding `0`, `""`, or an empty collection is overwritten, and
VALUE is not evaluated when the store is skipped.

**Examples:**

```koskari
>>> class Bag ():
...   pass
>>> var b (Bag)
>>> (unassigned? b.x)
True
>>> (default! b.x 5)
>>> b.x
5
>>> (default! b.x 9)
>>> b.x
5
```

```koskari
>>> var slot
>>> (unassigned? slot)
True
>>> (default! slot 7)
>>> slot
7
>>> (default! slot 9)
>>> slot
7
```

**See also:** [`ref`](compile-ref.md), [`set!`](compile-set.md),
[`bind!`](compile-bind.md).
