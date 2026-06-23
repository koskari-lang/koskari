# compile/yield
**Symbol:** `compile/yield`

**Syntax:** `(yield)` or `(yield VAL)`

**Use-case:** Yield a value from a generator (or coroutine). Suspends the
current frame and returns the value to the driver; resumption is via
`send(value)` or `throw()`. With no argument, yields `None`.

**Behaviour:** With no arg, emits `YIELD 0` (yield None). With one argument,
compiles VAL and emits `YIELD 1`. At runtime, when the frame has a parent that
is **not** the generator node (e.g. a nested CALL inside a generator), YIELD
detaches the frame, pushes the value onto the parent stack, and returns 0 so
the loop continues in the caller. When the frame's parent is the generator node
(KOS_DYN_GEN) or there is no parent, the opcode returns `EVAL_YIELD(val)`;
`apply_kos_op_result` routes to `eval_event_yield`, which runs
`kos_dyn_walk_handle_yield`, the generator node consumes the yield (freezes the
dynamic chain and suspends), sets `SUSPENSION_EVENT = YIELD`, and `koskari_eval`
returns `KOS_RES_YIELD`. On resume, `revive_yield` pushes `send_val` onto the stack.

**Examples:**

```koskari
(yield)
(yield (compute-next))
```

