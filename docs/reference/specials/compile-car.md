# compile/car

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/car`

**Syntax:** `(compile/car EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the car of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CAR 1**. At runtime, CAR applies car
once per unit of arg (see [OPCODES §5.1](OPCODES.md#kos_op_car)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CAR**
instructions may be merged by the compiler **compress** pass into one **CAR**
with the sum of their counts (same for **CDR**).

**Example:**

```scheme
(compile/car (compile/cons 1 nil))   ; → 1
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
