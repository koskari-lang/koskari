# compile/takeq
**Symbol:** `compile/takeq`

**Syntax:** `(takeq SYM)`

**Use-case:** Return the value of binding SYM and clear the slot. Unlike
`delq`, errors when the binding is empty (for example a declared-but-unassigned
local). Useful for returning an accumulator and dropping a gensym reference.

**Behaviour:** Requests the variable name for read (`request_var`), emits
VAR_TAKE (may lower to FAST_TAKE). The form value is the taken value; no CONST
None tail.

**See also:** [compile/delq](compile-delq.md) (idempotent clear, returns None).

**Examples:**

```koskari
(takeq temp)
```
