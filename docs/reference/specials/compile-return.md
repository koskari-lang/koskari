# compile/return
**Symbol:** `compile/return`

**Syntax:** `(return)` or `(return VAL)`

**Use-case:** Return a value from the current function and transfer control back to
the caller. With no argument, returns `None`.

**Behaviour:** With no arg, emits `RETURN 0` (return None). With one argument,
compiles VAL and emits `RETURN 1`. At runtime, when the frame has a parent (e.g.
normal CALL), RETURN walks the dynamic chain with handle(RETURN), runs FINALLY
if present, detaches the frame, and delivers the value to the parent (opcode
returns `EVAL_CONTROL_RETURNED()` or `EVAL_NORMAL()`). When the frame has no parent or
its parent is the generator node (KOS_DYN_GEN), RETURN returns `EVAL_RETURN(val)`;
`eval_event_return` delivers and `koskari_eval` exits with `KOS_RES_RETURN`.

**Examples:**

```koskari
(return)
(return result)
```

