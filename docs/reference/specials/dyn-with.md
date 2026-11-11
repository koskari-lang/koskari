# dyn-with
**Symbol:** `compile/dyn-with`

**Syntax:** `(dyn-with [name? expr] body ...)`

Same syntax as `with`; optional `name` binds the enter result.

**Use-case:** Context manager that a continuation transfer runs (shift/reset,
undelimited call/cc), as it runs a [`dyn-wind`](compile-dyn-wind.md): a
transfer that leaves the body calls exit, and one that resumes a captured
body calls enter again before the body continues, once per resumption. A
plain [`with`](with.md) is local to its body and a transfer runs neither.

**Behaviour:** Same as `with` except emits PUSH_DYN_WITH and POP_DYN_WITH.
The expression value is the body's value, or `None` when the exit swallows
an exception. See
[PUSH_DYN_WITH / POP_DYN_WITH](../vm/opcodes/kos-op-push-dyn-with.md) in the VM
reference.

**Examples:**

```koskari
;; Capture continuation inside dyn-with; invoke outside runs exit then enter
(dyn-with [r (make-resource)]
  (k (refq r)))
```
