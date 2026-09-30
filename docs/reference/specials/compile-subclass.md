# compile/subclass?
**Symbol:** `compile/subclass?`

**Syntax:** `(subclass? CLS BASE)`

**Use-case:** Whether `CLS` is a subclass of `BASE`, with the signature of
the runtime `subclass?` (Python's `issubclass`): `BASE` is one class or a
tuple of classes, and a tuple succeeds for any of them.

**Examples:**

```koskari
(subclass? list object)
(subclass? bool #(int str))
```

See [Object operator overview](object-operators/index.md).
