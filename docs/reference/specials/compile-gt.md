# compile/>
**Symbol:** `compile/>`
**Aliases:** `compile/gt?`

**Syntax:** `(> A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is ordered descending, like Python chained comparisons.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(> 3 2 1)
(> 1 3)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.
