# compile/!=
**Symbol:** `compile/!=`
**Aliases:** `compile/not-equal?`

**Syntax:** `(!= A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is unequal, like Python chained comparisons.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(!= 1 2)
(!= 1 1)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.

**Also:** operator function [`!=`](../runtime/operators.md#comparison).
