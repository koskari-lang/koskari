# compile/deref
**Symbol:** `compile/deref`

**Syntax:** `(deref EXPR)`

**Use-case:** Dereference a mutable reference. Compiles EXPR (which must produce
a Ref on the stack), then emits the DEREF opcode to call `ref.deref()` and push
the result. The runtime `deref` builtin remains available for higher-order use
(e.g. `(map deref refs)`).

**Behaviour:** Compiles EXPR, emits KOS_OP_DEREF. Fast path; avoids a full call.

**Examples:**

```koskari
(deref r)   ; compiles expr for r, emits DEREF
(deref r)           ; runtime call; use when passing deref as first-class fn
```

**Also:** function [`deref`](../runtime/functions.md#refs-parameters-and-the-environment).
