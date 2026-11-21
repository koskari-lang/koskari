# with
**Symbol:** `compile/with`

**Syntax:** `(with [name? expr] body ...)`

Optional `name` binds the context manager's enter result for the body. If
omitted, only `expr` and `body ...` are given (no binding).

**Use-case:** Context manager local to its body: evaluate `expr` to get a
manager, call enter (auto-await if needed), run body in a fresh operand stack
block, then call exit when the body completes, raises, or is left by a
`return`, `break` or `continue`. A continuation transfer runs neither enter
nor exit: a `call/cc` escape out of the body, or a `shift` that aborts the
region the `with` is in, leaves the manager entered, and resuming a captured
body does not call enter again. Each copy of the body that a continuation
resumes is still inside the manager and calls exit when that copy exits.
Use [dyn-with](dyn-with.md) for a manager that a transfer must exit and
enter again.

**Behaviour:** Compiles `expr`, emits PUSH_WITH with optional name (constant
pool index or no-binding sentinel), compiles the body as a `begin`, emits
POP_WITH. The expression value is the body's value; when the exit swallows an
exception, evaluation continues after the `with` and the value is `None`. The
exit's own return decides only whether the exception is swallowed and is never
the value. Both sync and async context managers
are supported (VM auto-awaits enter/exit when they return awaitables). See
[dyn-with](dyn-with.md) for the variant a transfer runs and
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
