# compile/cdr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/cdr`

**Syntax:** `(compile/cdr EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the cdr of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CDR 1**. At runtime, CDR applies cdr
once per unit of arg (see [OPCODES §5.1](OPCODES.md#kos_op_cdr)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CDR**
instructions may be merged by the compiler **compress** pass into one **CDR**
with the sum of their counts (same for **CAR**).

**Example:**

```scheme
(compile/cdr (compile/cons 1 (compile/cons 2 nil)))   ; → (2 . nil)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
