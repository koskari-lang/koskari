# Koskari: `with` and `dyn-with` (Context Manager) Design

This document specifies the context-manager forms that stress-test dynamic-extent
extensibility while keeping semantics crisp and VM-friendly. It defines the
opcode contracts, stack/block behaviour, dynamic-chain integration, and
transfer-plan rules.

**See also:** [OPCODES.md](../OPCODES.md) (opcode reference), [extent-wind-unwind.md](extent-wind-unwind.md)
(unified wind/unwind model), [execution-invariants.md](execution-invariants.md).


## Goal

- **`with`**: Single-shot, extent-only unwind. Safe for normal control-flow and
  exception unwind; **not** safe across continuation transfers.
- **`dyn-with`**: Multi-shot, full wind/unwind participation for non-local
  transfers (shift/reset and undelimited call/cc), including re-entry.

Both forms are **auto-async**: enter/exit may return awaitables; the VM flattens
them like CALL.


## Surface forms

### `with`

```
(with [name? expr] body...)
```

- `expr` evaluates to a context-manager-like object `mgr`.
- Optional `name` binds the enter value.
- The overall expression value is **always** `None`.

### `dyn-with`

```
(dyn-with [name? expr] body...)
```

Same as `with`, but the dynamic extent is multi-shot and participates in
continuation transfer plans.


## Bytecode / opcodes

Two opcode families, each taking an optional binding name (or `None`) as
argument.

### Single-shot opcodes

- `PUSH_WITH <name-or-None>`
- `POP_WITH`

### Multi-shot opcodes

- `PUSH_DYN_WITH <name-or-None>`
- `POP_DYN_WITH`

The argument is a constant-pool reference to a symbol/string name, or a
dedicated `None` sentinel. See [OPCODES.md §18.1](../OPCODES.md#181-context-managers-with--dyn-with).


## Dynamic chain nodes

Dedicated dyn node kinds:

- `KOS_DYN_WITH` — single-shot context manager
- `KOS_DYN_DYN_WITH` — multi-shot context manager

### Node payload (minimum)

- `mgr`: the context manager object
- `enter`: resolved enter callable (sync or async, chosen by presence heuristic;
  cached)
- `exit`: resolved exit callable (sync or async; recommended to cache)
- `entered`: bool (false until enter succeeds)
- `name`: optional binding name (or null)
- `resume_pc`: PC to resume at after the with body (pop_pc + 1); stored at push
  time from the WITH extent, so the evaluator does not look it up at unwind
- `stack_block`: reference/handle to the operand-stack block created for the body
- `mode`: SINGLE or MULTI

### Invariants

- The node owns exactly one operand stack block.
- Enter/exit are invoked at most once per dynamic extent instance (except MULTI
  re-entry, which creates a new instance on re-wind).
- Stack restoration is structural: pop the owned stack block; no height-mark
  arithmetic.


## Stack / block semantics

Koskari’s operand stack is block-structured. `with` and `dyn-with` push a fresh
block for the body.

### Rationale

- The body may abend (exception or non-local exit).
- The manager may swallow an exception.
- An independent operand stack segment ensures a clean, deterministic restore
  without tracking marks.

### Result value

- The overall expression result is **always** `None`.
- Any value needed from the body must be stored explicitly (e.g. `setq`) before
  leaving.


## Enter/exit protocol and transparent async

### Method names

The VM resolves context-manager methods by name. Standard names are the Python
attribute names: `__enter__`, `__aenter__`, `__exit__`, `__aexit__`. These are
the conventional names; the VM looks them up on the manager object.

### Sync vs async heuristic

Protocol is chosen at **runtime** by checking for **presence** of the async
methods. If the manager has `__aenter__` (and typically `__aexit__`), use the
async protocol; otherwise use `__enter__`/`__exit__`. Koskari uses a single
opcode path for both `with` and `dyn-with`; the compiler does not choose
sync vs async (unlike sibilant-ceval, which had separate `with` and
`await-with` opcodes).

### Caching

Cache the resolved callables (or the protocol choice plus bound methods) so we
avoid repeated lookup. The node payload’s `enter` and `exit` fields hold these
**resolved** callables (either the sync or async pair) after the presence check.

### Prior art

In sibilant-ceval, two opcodes (PUSH_WITH and PUSH_AWAIT_WITH) were used and
handlers were resolved via `PyObject_VectorcallMethod` with the four names
`__enter__`, `__aenter__`, `__exit__`, `__aexit__` (see sibceval/lib/ceval.c:
context_enter, context_exit, get_await_enter, get_await_exit, and init).
Koskari uses a single opcode path and chooses protocol by presence check (and
cache) instead of at compile time.

### Koskari callables

The resolved enter/exit may be **Koskari callables** (e.g. KosFunction or
KosExpression), not only Python callables. Invoke them via normal **CALL**
semantics: the evaluator runs the call to completion. Any internal **AWAIT** is
handled by the existing drive loop (transparent async). So “flatten” or “inline”
means no special execution mode — we CALL and run until return (or exception).
The bound methods (or wrapper callables) are what we store and call; if they
return an awaitable (e.g. from Python or from Koskari code that yields one), we
auto-await as usual.


## Opcode contracts

### PUSH_WITH \<name-or-None\>

**Stack before:** `[..., mgr]`

**Behaviour:**

1. Pop `mgr`.
2. Resolve enter/exit by protocol heuristic (`__enter__`/`__exit__` vs
   `__aenter__`/`__aexit__`); cache at least exit (and enter if desired).
3. Call `enter`; if it returns an awaitable, auto-await.
4. On success, set `entered = true`.
5. If `name` provided: bind enter result into the current namespace under `name`.
6. Push a new operand stack block for the body.
7. Look up the WITH extent at the current PC; compute `resume_pc = pop_pc + 1`.
8. Push `KOS_DYN_WITH` on the dynamic chain with `resume_pc` stored on the node.

**Stack after:** Body executes in a fresh stack block.

**Failure in enter:** If `enter` raises, no dyn node is installed, no stack block
is pushed, and the exception propagates.


### POP_WITH

**Behaviour (normal exit):**

1. Call `exit` in the no-exception form (see “Exit calling convention”).
   Auto-await if needed.
2. Pop the operand stack block owned by the node.
3. Pop the dyn node.
4. Push `None` as the value of the `with` expression.

**Behaviour (exception unwinding):** When leaving due to an exception inside the
body, the runtime enters the exception path for the nearest active `with`
extent:

1. Call `exit(exc_type, exc_val, tb)`; auto-await if needed.
2. If `exit` indicates **swallow**:
   - clear exception state
   - pop the owned stack block
   - pop the dyn node
   - push `None`
   - set frame PC to the node's `resume_pc` (instruction after POP_WITH)
   - resume execution at the post-with continuation point
3. Otherwise:
   - pop the owned stack block
   - pop the dyn node
   - re-raise / propagate


### PUSH_DYN_WITH \<name-or-None\>

Same as PUSH_WITH, except:

- Installs `KOS_DYN_DYN_WITH`.
- The node is eligible for inclusion in continuation transfer plans.


### POP_DYN_WITH

Same as POP_WITH for normal and exception unwind.


## Exit calling convention

The VM always uses the Python convention:

- `exit(exc_type, exc_val, tb)`; return value truthy means swallow.
- Normal exit passes `(None, None, None)`.


## Continuation / non-local transfer rules

### Rule 1: Single-shot `with` is a transfer barrier

If a continuation transfer (delimited or undelimited) would cross an active
`KOS_DYN_WITH` node:

- **Reject** the transfer.
- Raise a dedicated runtime error (e.g. `KoskariIllegalTransferAcrossWith`).

This preserves the single-shot contract and prevents resource unsafety.

### Rule 2: `dyn-with` participates in transfer plans

For `KOS_DYN_DYN_WITH` nodes, the transfer-plan builder must include them as
wind/unwind steps:

- **Unwind step:** run `exit` (exception or non-exception form as appropriate
  for the transfer)
- **Wind step:** run `enter`; re-bind `name` if present

**Transfer plan suspension:** Enter/exit may suspend (auto-await). When a wind
or unwind step returns an awaitable, the plan may suspend. The implementation
must record progress (e.g. which steps completed, which step is next) and
resume the plan when the await completes. Transfer plans must therefore support
**suspension and resumption** (interruptible plans); this is a requirement for
dyn-with and likely for other async extents as well. The exact state
representation (e.g. plan-in-progress struct or step index) is not specified
here; the plan must be resumable.

**Wind/unwind ordering:** Unwind runs exit steps from **innermost** to
**outermost**. Wind runs enter steps from **outermost** to **innermost**. This
matches the ordering described in [extent-wind-unwind.md](extent-wind-unwind.md)
for other extent kinds; no special case for dyn-with.

**Notes:**

- Re-entry creates a fresh dynamic extent instance; multi-shot continuations
  must not share mutable node state.


## Interaction with other extents

### Exceptions

- With extents must be discoverable during exception dispatch/unwind.
- Swallowing must clear the exception state and resume at the post-with point.

### Loops (break/continue)

- If break/continue exits the with body, treat as normal non-exception exit: run
  `exit(no-exc)` and restore the stack block.

### Yield / yield-from

- If leaving the with body due to generator close/throw, `exit` must run as
  part of unwind.


## Compiler notes

- `PUSH_*_WITH <name>` should subsume the “bind enter result” behaviour so the
  compiler does not need to emit separate VAR_BIND sequences.
- The with body should compile as a normal block that ends with a `POP_*_WITH`
  at the structurally correct exit point(s).
- Since the overall expression value is always `None`, the compiler may omit
  carrying the body’s last value.


## Required tests (minimal)

### Single-shot

1. Normal enter/exit; expression returns None.
2. Exception inside body; exit sees exception; swallow = false ⇒ exception propagates.
3. Exception swallowed; stack restored; execution resumes; expression yields None.
4. Break/continue exits body; exit runs; stack restored.
5. Continuation transfer across `with` ⇒ explicit error.

### Multi-shot

1. Continuation capture inside `dyn-with`; invoke outside; enter/exit run on
   transfer; binding correct.
2. Multi-shot invoke twice; no shared mutable state; no double-exit.
3. Async enter/exit that suspends; correct resume.


## Non-negotiable invariants

- Stack restoration for with bodies is structural (block push/pop), never
  arithmetic marking.
- `with` is single-shot and cannot be crossed by continuation transfer.
- `dyn-with` is multi-shot and must wind/unwind on non-local transfer.
- Expression value is always `None`.
- Enter failure installs no dyn node and no stack block.
- Swallowing clears exception state and resumes normally.
