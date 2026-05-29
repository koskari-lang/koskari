# compile/return

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/return`

**Syntax:** `(compile/return)` or `(compile/return VAL)`

**Use-case:** Return a value from the current function and transfer control back to
the caller. With no argument, returns `None`.

**Behaviour:** With no arg, emits `RETURN 0` (return None). With one argument,
compiles VAL and emits `RETURN 1`. At runtime, when the frame has a parent (e.g.
normal CALL), RETURN walks the dynamic chain with handle(RETURN), runs FINALLY
if present, detaches the frame, and delivers the value to the parent (opcode
returns `KOS_OP_RES_CONTROL_RETURNED` or 0). When the frame has no parent or
its parent is the generator node (KOS_DYN_GEN), RETURN produces a boundary
event `KOS_EV_RETURN` and the evaluator exits.

**Examples:**

```scheme
(compile/return)
(compile/return result)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
