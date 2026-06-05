# compile/car
**Symbol:** `compile/car`

**Syntax:** `(car EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the car of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CAR 1**. At runtime, CAR applies car
once per unit of arg (see [KOS_OP_CAR](../vm/opcodes/kos-op-car.md)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CAR**
instructions may be merged by the compiler **compress** pass into one **CAR**
with the sum of their counts (same for **CDR**).

**Example:**

```koskari
(car (cons 1 nil))   ; → 1
```

