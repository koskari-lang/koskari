# compile/==
**Symbol:** `compile/==`
**Aliases:** `compile/equal?`

**Syntax:** `(== A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is equal, like Python chained comparisons.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(== 1 1)
(== 5 5 7 5)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.
