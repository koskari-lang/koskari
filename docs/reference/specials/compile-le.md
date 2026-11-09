# compile/le?
**Symbol:** `compile/le?`
**Aliases:** `compile/<=`

**Syntax:** `(le? A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is ordered ascending or equal. Every operand is
evaluated, left to right, before the pairs are compared.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(le? 1 2)
(le? 2 1)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.

**Also:** operator function [`le?`](../runtime/operators.md#comparison).
