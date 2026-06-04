# compile/not-equal?
**Symbol:** `compile/not-equal?`
**Aliases:** `compile/!=`

**Syntax:** `(not-equal? A B ...)`

**Use-case:** Chained comparison — at least two values. Succeeds when
each adjacent pair is unequal, like Python chained comparisons.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(not-equal? 1 2)
(not-equal? 1 1)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.
