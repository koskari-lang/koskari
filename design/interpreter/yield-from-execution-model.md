# Koskari `YIELD_FROM` Execution Model

## Purpose

`YIELD_FROM` is a single-opcode delegation primitive. It is not loop sugar. It implements transparent generator/iterator delegation with full support for:

* value propagation (yielded values)
* `send()` propagation
* `throw()` propagation
* `close()` propagation
* async flattening (Python + Koskari async generators)

It must behave as a pass-through conduit between caller and delegate.

---

## High-Level Semantics

Given:

```
Foo -> Bar -> (yield-from Baz)
```

Delegation rules:

* Values flow: Baz → Foo
* `send` / `throw` / `close` flow: Foo → Baz
* Bar regains control only when:

  * Baz yields (to forward),
  * Baz completes (to return final value),
  * Baz propagates an exception.

If Foo throws into Bar at a suspended `yield-from`, the exception is injected into Baz first. Only if Baz does not handle it does it propagate upward.

---

## Operand Stack Contract

On first entry:

```
[..., subexpr_result]
```

`YIELD_FROM` consumes `subexpr_result` and delegates to it.

On completion:

```
[..., final_value]
```

The opcode does **not** advance the program counter until delegation completes.

---

## Dynamic Extent Design

`YIELD_FROM` uses a dedicated dynamic region node:

```
KOS_DYN_YIELD_FROM
```

This node stores persistent delegation state.

### Required Fields

* `PyObject *delegate` — normalized delegate object
* `uint8_t kind` — enum:

  * SYNC_ITER
  * SYNC_GEN
  * PY_ASYNC_ITER
  * PY_ASYNC_GEN
  * KOS_ASYNC_GEN
* `uint8_t pending_action` — STEP | SEND | THROW | CLOSE
* `PyObject *pending_arg` — value or exception
* `PyObject *inflight` — awaitable / async stepping state (if needed)

The dyn node is the single source of truth for delegation state.

---

## Program Counter Discipline

* On first execution:

  * Pop TOS
  * Normalize to delegate
  * Push `KOS_DYN_YIELD_FROM`
  * Do NOT advance PC

* On each resume:

  * Re-enter the same opcode
  * Consult dyn node
  * Drive delegate

* Only when delegate completes:

  * Pop dyn node
  * Push final value
  * Advance PC

This makes `YIELD_FROM` a micro-coded state machine.

---

## Delegate Classification

### SYNC_ITER

* Use `tp_iternext`
* `send(value)` only valid if value is None
* No `throw()` unless object supports it

### SYNC_GEN

* Use `.send(value)`
* Use `.throw(exc)`
* Use `.close()`
* StopIteration(value) yields completion value

### PY_ASYNC_ITER / PY_ASYNC_GEN

* Use `__anext__` / `asend` / `athrow` / `aclose`
* Produce awaitable
* Hand awaitable to existing `AWAIT` machinery
* Immediate-completion probe allowed
* StopAsyncIteration signals completion

### KOS_ASYNC_GEN

* Drive using in-loop flatten attach/detach
* No recursive C evaluation
* Completion produces final value

---

## Resume Injection

The evaluator provides a resume kind:

* STEP (normal resume)
* SEND(value)
* THROW(exception)
* CLOSE

`YIELD_FROM` must forward resume actions into the delegate without interpretation.

---

## Error Propagation

Rules:

1. THROW from caller is injected into delegate.
2. If delegate handles it, delegation continues.
3. If delegate propagates, error bubbles outward.
4. Bar may catch only by wrapping the `yield-from` site in `try/except`.

The dyn node may be popped either:

* eagerly on error, or
* by general unwinder logic.

---

## Continuation Compatibility

Because delegation state lives in a dynamic extent node:

* Continuation capture includes yield-from state.
* Resumption re-enters the same opcode.
* No operand-stack trickery required.

This preserves undelimited continuation correctness.

---

## Design Constraints

* No hidden stack layouts.
* No recursive eval calls.
* Async flattening must remain single-evaluator.
* Opcode remains NOARG.
* Delegation is transparent by default.

If intermediary interpretation is required, implement explicit loop + `yield`, not `YIELD_FROM`.

---

## Summary

`YIELD_FROM` is a PC-stable delegation state machine backed by a dynamic extent node. It forwards all data and control flows transparently to the delegate and completes only when the delegate completes.

It is not a loop. It is a conduit.
