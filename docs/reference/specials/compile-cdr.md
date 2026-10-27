# compile/cdr
**Symbol:** `compile/cdr`

**Syntax:** `(cdr EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the cdr of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CDR 1**. At runtime, CDR applies cdr
once per unit of arg (see [KOS_OP_CDR](../vm/opcodes/kos-op-cdr.md)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CDR**
instructions may be merged by the compiler **compress** pass into one **CDR**
with the sum of their counts (same for **CAR**).

**Example:**

```koskari
(cdr (cons 1 (cons 2 nil)))   ; -> (2 . nil)
```

**Also:** function [`cdr`](../runtime/functions.md#pairs).
