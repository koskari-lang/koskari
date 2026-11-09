# compile/>=
**Symbol:** `compile/>=`
**Aliases:** `compile/ge?`

**Syntax:** `(>= A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is ordered descending or equal. Every operand is
evaluated, left to right, before the pairs are compared.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(>= 3 3 2)
(>= 1 3)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.

**Also:** operator function [`>=`](../runtime/operators.md#comparison).
