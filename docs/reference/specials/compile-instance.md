# compile/instance?
**Symbol:** `compile/instance?`

**Syntax:** `(instance? OBJ TYPE)`

**Use-case:** Runtime type check, with the signature of the runtime
`instance?` (Python's `isinstance`): `TYPE` is one type or a tuple of
types, and a tuple succeeds if `OBJ` is an instance of any of them.

**Examples:**

```koskari
(instance? 42 int)
(instance? "hi" #(str bytes))
```

See [Object operator overview](object-operators/index.md).
