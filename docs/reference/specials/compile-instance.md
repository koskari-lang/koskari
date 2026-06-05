# compile/instance?
**Symbol:** `compile/instance?`

**Syntax:** `(instance? OBJ TYPE ...)`

**Use-case:** Runtime type check — at least value and one type. With several
types, succeeds if `OBJ` is an instance of any of them.

**Examples:**

```koskari
(instance? 42 int)
(instance? "hi" str bytes)
```

See [Object operator overview](object-operators/index.md).
