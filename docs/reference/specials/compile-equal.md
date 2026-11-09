# compile/equal?
**Symbol:** `compile/equal?`
**Aliases:** `compile/==`

**Syntax:** `(equal? A B ...)`

**Use-case:** Chained comparison -- at least two values. Succeeds when
each adjacent pair is equal. Every operand is evaluated, left to right,
before the pairs are compared.

**Behaviour:** Compiles each argument, emits the corresponding `OBJECT_*`
opcode with operand count.

**Examples:**

```koskari
(equal? 1 1)
(equal? 0 1)   ; false
```

See [Object operator overview](object-operators/index.md) for the full comparison table.

**Also:** operator function [`equal?`](../runtime/operators.md#comparison).
