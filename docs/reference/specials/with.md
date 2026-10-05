# with
**Symbol:** `compile/with`

**Syntax:** `(with [name? expr] body ...)`

Optional `name` binds the context manager's enter result for the body. If
omitted, only `expr` and `body ...` are given (no binding).

**Use-case:** Single-shot context manager: evaluate `expr` to get a manager,
call enter (auto-await if needed), run body in a fresh operand stack block,
then call exit on normal or exception exit. A continuation transfer out of
the body runs the manager's exit as an exception would, and the instance is
finished: single-shot means each `with` instance enters once and exits once,
and a transfer back into a captured body does not enter it again. Use
[dyn-with](dyn-with.md) for a manager that should wind and unwind on every
transfer.

**Behaviour:** Compiles `expr`, emits PUSH_WITH with optional name (constant
pool index or no-binding sentinel), compiles body with `helper_begin`, emits
POP_WITH. PUSH_WITH looks up the WITH extent at its PC and stores
`resume_pc = pop_pc + 1` on the node for use when exit swallows an exception.
The expression value is the body's value; when the exit swallows an exception
the value is `None`. The exit's own return decides only whether the exception
is swallowed and is never the value. Both sync and async context managers
are supported (VM auto-awaits enter/exit when they return awaitables). See
[dyn-with](dyn-with.md) for the multi-shot variant and
[PUSH_WITH / POP_WITH](../vm/opcodes/kos-op-push-with.md) in the VM reference.

**Examples:**

```koskari
;; With binding
(with [f (open-file path)]
  (read-all f))

;; Without binding
(with [(make-lock)]
  (do-work))
```
