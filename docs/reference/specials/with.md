# with

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/with`

**Syntax:** `(with [name? expr] body ...)`

Optional `name` binds the context manager's enter result for the body. If
omitted, only `expr` and `body ...` are given (no binding).

**Use-case:** Single-shot context manager: evaluate `expr` to get a manager,
call enter (auto-await if needed), run body in a fresh operand stack block,
then call exit on normal or exception exit. Safe for normal control-flow and
exception unwind; **not** safe across continuation transfers (transfer across
an active `with` raises a runtime error).

**Behaviour:** Compiles `expr`, emits PUSH_WITH with optional name (constant
pool index or no-binding sentinel), compiles body with `helper_begin`, emits
POP_WITH. PUSH_WITH looks up the WITH extent at its PC and stores
`resume_pc = pop_pc + 1` on the node for use when exit swallows an exception.
The expression value is always `None`. Both sync and async context managers
are supported (VM auto-awaits enter/exit when they return awaitables). See
[interpreter/with-dyn-with.md](../../design/interpreter/with-dyn-with.md).

**Tests:** Opcode tests (`tests/lib/opcode/test_with.py`) and special-form
tests (`tests/compile/specials/test_with.py`) cover sync and async
enter/exit, normal completion, and exception propagation/swallow.

**Examples:**

```scheme
;; With binding
(with [f (open-file path)]
  (compile/call read-all f))

;; Without binding
(with [(make-lock)]
  (compile/call do-work))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
