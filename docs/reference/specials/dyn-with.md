# dyn-with
**Symbol:** `compile/dyn-with`

**Syntax:** `(dyn-with [name? expr] body ...)`

Same syntax as `with`; optional `name` binds the enter result.

**Use-case:** Multi-shot context manager that participates in continuation
transfer plans (shift/reset, undelimited call/cc). On non-local transfer,
enter/exit run as wind/unwind steps; re-entry creates a fresh extent
instance.

**Behaviour:** Same as `with` except emits PUSH_DYN_WITH and POP_DYN_WITH.
Expression value is always `None`. See
[PUSH_DYN_WITH / POP_DYN_WITH](../vm/index.md#kos_op_push_dyn_with) in the VM
reference.

**Examples:**

```koskari
;; Capture continuation inside dyn-with; invoke outside runs exit then enter
(dyn-with [r (make-resource)]
  (k (refq r)))
```

