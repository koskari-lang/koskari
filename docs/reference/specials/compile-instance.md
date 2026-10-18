# compile/instance?
**Symbol:** `compile/instance?`

**Syntax:** `(instance? OBJ TYPE ...)`

**Use-case:** Runtime type check against one or more types, succeeding
when `OBJ` is an instance of any of them. A type operand may be a tuple
of types, opened one level: a tuple inside a tuple raises `TypeError`.
A tuple written as a literal is spliced into the operands at compile
time. The runtime `instance?` has the same signature.

**Examples:**

```koskari
(instance? 42 int)
(instance? "hi" str bytes)
(instance? "hi" #(str bytes))
(instance? x Number str)
```

In the last form `Number` may name a single type or a tuple of types.

See [Object operator overview](object-operators/index.md).
