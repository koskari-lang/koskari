# compile/subclass?
**Symbol:** `compile/subclass?`

**Syntax:** `(subclass? CLS BASE ...)`

**Use-case:** Whether `CLS` is a subclass of any of one or more bases.
A base operand may be a tuple of classes, opened one level: a tuple
inside a tuple raises `TypeError`. A tuple written as a literal is
spliced into the operands at compile time. The runtime `subclass?` has
the same signature.

**Examples:**

```koskari
(subclass? list object)
(subclass? bool int str)
(subclass? bool #(int str))
```

See [Object operator overview](object-operators/index.md).

**Also:** function [`subclass?`](../runtime/functions.md#predicates).
