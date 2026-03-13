# YIELD_FROM Revamped Operational Model

## Summary

This document specifies a redesigned YIELD_FROM implementation in which the state machine lives entirely within the KosDynYieldFrom node. YIELD_FROM acts as a **super-operator** form of YIELD: it only ever emits `KOS_EV_YIELD` or `KOS_EV_AWAIT`, never a distinct YIELD_FROM boundary. The context treats it uniformly with plain YIELD and AWAIT. Resume inject (send value, await result) is delivered via the standard revive path: `revive_yield` or `revive_await` pushes the value onto the stack. The YIELD_FROM handler pops it from TOS and forwards it to the delegate. No `KOS_SUSP_YIELD_FROM`, no `revive_yield_from`, and no `PY_RESUME_KIND` consumption by YIELD_FROM.

---

## Revision History

- 2025-03-13: Initial design for node-driven YIELD_FROM; supersedes resume-inject mechanism in prior yield-from-execution-model.md.

---

## Part 1: Design Goals and Rationale

### Goals

1. **State machine in the node** — All YIELD_FROM state and transition logic lives in `KosDynYieldFrom`. The eval loop has no YIELD_FROM-specific branches beyond the opcode handler.
2. **Unified boundary events** — YIELD_FROM emits only `KOS_EV_YIELD` (value to caller) or `KOS_EV_AWAIT` (suspend on awaitable). The eval loop does not distinguish YIELD_FROM from plain YIELD or AWAIT.
3. **Simplified eval loop** — Remove `KOS_SUSP_YIELD_FROM`, `revive_yield_from`, and YIELD_FROM's use of `KosContext_PY_RESUME_KIND`. YIELD_FROM consumes inject from the stack via node state.
4. **FOREACH-like pattern** — Transform TOS to iterator (delegate), drive it step-by-step, handle "has next" / "awaiting next" / "yield next" / "exhausted" in the node.
5. **PC-stable** — PC stays at the YIELD_FROM instruction until delegation completes. The handler rolls back PC on every suspend (yield or await).

### What Stays the Same

- Valid delegates and rejected targets (coroutines rejected; iterables and async-iterables accepted).
- Delegate kinds: SYNC_ITER, SYNC_GEN, PY_ASYNC_ITER, PY_ASYNC_GEN, KOS_ASYNC_GEN.
- Normalization logic (`yield_from_normalize`).
- Semantics: value propagation, send/throw/close forwarding, delegate exhaustion.
- Node layout (delegate, kind, pending_arg, pending_arg_type, inflight) plus one new field for phase.

---

## Part 2: Node Structure and Phase Enum

### Phase Field

Add an explicit phase to the node to discriminate resume handling without context-level state:

```c
enum KosYfPhase {
  KOS_YF_PHASE_DRIVING = 0,   /* normal: driving delegate steps */
  KOS_YF_PHASE_EXPECT_SEND,   /* last action was yield; next TOS is send value */
  KOS_YF_PHASE_EXPECT_AWAIT,  /* last action was await; next TOS is await result */
};
```

Store as `uint8_t phase` in the yield_from union. Macros:

- `KosDynYieldFrom_PHASE(node)`
- `KosDynYieldFrom_PHASE_SET(node, v)`

### When to Set Phase

- **KOS_YF_PHASE_DRIVING**: Default. Set when: node creation, after consuming inject, after completing a step that did not suspend.
- **KOS_YF_PHASE_EXPECT_SEND**: Set when the handler is about to return 1 with `KOS_EV_YIELD`. Indicates "on next entry, TOS will be the send value from revive_yield".
- **KOS_YF_PHASE_EXPECT_AWAIT**: Set when the handler is about to return 1 with `KOS_EV_AWAIT`. Indicates "on next entry, TOS will be the await result from revive_await".

### Existing Fields (unchanged)

- `delegate` — normalized iterator/generator
- `kind` — KOS_YF_SYNC_ITER, KOS_YF_SYNC_GEN, KOS_YF_PY_ASYNC_ITER, KOS_YF_PY_ASYNC_GEN, KOS_YF_KOS_ASYNC_GEN
- `pending_action` — KOS_YF_STEP, KOS_YF_SEND, KOS_YF_THROW, KOS_YF_CLOSE
- `pending_arg` — value to send or exception
- `pending_arg_type` — exception type for KOS_YF_THROW
- `inflight` — Python awaitable when suspended on delegate's __anext__/asend/athrow/aclose

### Layout Change

Add `uint8_t phase` (and pad if needed for alignment). Keep all other fields. The struct is extended, not redesigned.

---

## Part 3: Handler Control Flow

### Entry Branches (in order)

1. **No yield_from node (first entry)**
   - Pop TOS.
   - Call `yield_from_normalize(subexpr, &delegate, &kind)`.
   - On error: DECREF subexpr, return -1.
   - Push node via `kos_dyn_push_yield_from(ctx, delegate, kind)`.
   - DECREF delegate.
   - Set `PENDING_ACTION = KOS_YF_STEP`, `PHASE = KOS_YF_PHASE_DRIVING`.
   - Roll back PC to this instruction.
   - Return 0 (continue loop; next iteration re-dispatches to YIELD_FROM).

2. **INJECT_MODE == KOS_INJECT_THROW**
   - Consume inject: clear INJECT_MODE, INJECT_VALUE, INJECT_TYPE.
   - Store exception in PENDING_ARG, PENDING_ARG_TYPE.
   - Set PENDING_ACTION = KOS_YF_THROW, PHASE = KOS_YF_PHASE_DRIVING.
   - Fall through to drive.

3. **PHASE == KOS_YF_PHASE_EXPECT_SEND**
   - Pop TOS into PENDING_ARG (the send value from revive_yield).
   - Set PENDING_ACTION = KOS_YF_SEND, PHASE = KOS_YF_PHASE_DRIVING.
   - Fall through to drive.

4. **PHASE == KOS_YF_PHASE_EXPECT_AWAIT**
   - Pop TOS into PENDING_ARG (the await result from revive_await). Do not clear INFLIGHT; the drive uses INFLIGHT to detect "resuming from await" and uses PENDING_ARG as the await result.
   - Set PENDING_ACTION = KOS_YF_STEP, PHASE = KOS_YF_PHASE_DRIVING.
   - Fall through to drive. The drive sees INFLIGHT set and PENDING_ARG set, handles "resuming from await" (clears INFLIGHT, uses PENDING_ARG as the result), then emits YIELD or continues as appropriate.

5. **Default (DRIVING)**
   - Use PENDING_ACTION (and PENDING_ARG if relevant).
   - Call the appropriate drive function.

### Drive Functions (behavior unchanged)

The existing drive functions (`yield_from_drive_sync_iter`, `yield_from_drive_sync_gen`, `yield_from_drive_py_async`, `yield_from_drive_kos_async_gen`) keep their logic. The only changes:

- **Before emitting YIELD**: Set `PHASE = KOS_YF_PHASE_EXPECT_SEND`.
- **Before emitting AWAIT**: Set `PHASE = KOS_YF_PHASE_EXPECT_AWAIT`. (INFLIGHT is already set by the drive.)

No other changes to drive logic.

---

## Part 4: Resume Inject Delivery (Critical)

### Eval Loop Convention

On resume from YIELD or AWAIT:

- `revive_yield(ctx, send_val, ...)` pushes `send_val` onto the current frame stack when `send_val != NULL`.
- `revive_await(ctx, send_val, ...)` pushes the await result onto the stack. The value to push is:
  - `KosContext_PY_RESUME_INJECT(ctx)` if set (driver pre-stored the await result, e.g. coro.c throw-into-awaitable path); then clear it.
  - Else `send_val` if non-NULL (normal path: caller passes result as send_val).
- If neither is available, nothing is pushed. YIELD_FROM treats that as inject = Py_None.

**Prerequisite**: Ensure `revive_await` pushes the await result. If the current implementation only pushes `send_val`, update it to push `PY_RESUME_INJECT` when set (and clear it), otherwise `send_val`. This ensures YIELD_FROM always receives the inject at TOS when PHASE is EXPECT_AWAIT.

### Stack Layout on Resume

When YIELD_FROM suspended with a yield:

- Before suspend: we pushed the yielded value, set payload, returned 1. Stack = `[..., yielded_val]`.
- Revive pushes send_val: stack = `[..., yielded_val, send_val]`.
- TOS = send_val. We pop it and store in PENDING_ARG.

When YIELD_FROM suspended with an await:

- Before suspend: we did not push anything; we emitted KOS_EV_AWAIT with the awaitable as payload.
- Revive pushes the await result: stack = `[..., await_result]`.
- TOS = await_result. We pop it into PENDING_ARG. The drive expects INFLIGHT set and PENDING_ARG = await result.

### Cleanup of Yielded Value

After we emit YIELD, the stack holds the yielded value (we used TailPtr for the payload, did not pop). On resume, revive pushes send_val, so we have `[..., yielded_val, send_val]`. We pop send_val into PENDING_ARG, then pop the stale yielded value to discard. The emit helper does not pop (it uses TailPtr); we clean up on resume with two pops.

**Decision**: On PHASE_EXPECT_SEND, pop TOS into PENDING_ARG. If the stack has another element (the stale yielded value), pop it to discard. If the first pop fails (empty stack), return -1. If the second pop would underflow, skip it (stack had only one element — revive pushed nothing, and we had no stale value; use PENDING_ARG as-is).

**EXPECT_AWAIT, empty stack**: If revive pushed nothing, stack may be empty. Do not pop. Set PENDING_ARG = Py_None, leave INFLIGHT set, proceed to drive.

---

## Part 5: PC Rollback

YIELD_FROM always rolls back PC when suspending (yield or await). The handler calls `yield_from_rollback_pc(ctx, insn)` before returning 1. The eval loop's `eval_phase_post_opcode` does **not** add a YIELD_FROM rollback; the handler performs it. No change.

---

## Part 6: Eval Loop Simplifications

### Remove

1. **KOS_SUSP_YIELD_FROM** — Delete from `KosSuspensionEvent` enum. YIELD_FROM emits KOS_EV_YIELD or KOS_EV_AWAIT, so SUSPENSION_EVENT becomes KOS_SUSP_YIELD or KOS_SUSP_AWAIT only.
2. **revive_yield_from** — Delete. Both YIELD and YIELD_FROM use revive_yield (for yield) or revive_await (for await).
3. **raise_yield_from** — Delete. Use raise_yield or raise_await.
4. **YIELD_FROM branches in eval_phase_entry** — Remove the `case KOS_SUSP_YIELD_FROM` branches. The switch will no longer mention YIELD_FROM.
5. **PY_RESUME_KIND consumption by YIELD_FROM** — The YIELD_FROM handler no longer checks or clears `KosContext_PY_RESUME_KIND`. The loop-top clear `if (KosContext_PY_RESUME_KIND(ctx) != KOS_RESUME_NONE) KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE` stays (it's for AWAIT resume from coro.c), but YIELD_FROM does not use it.

### Keep

- `revive_yield` unchanged.
- `revive_await` unchanged in structure; add logic to push `PY_RESUME_INJECT` when set (see Part 4).
- SUSPENSION_EVENT assignment: KOS_EV_YIELD → KOS_SUSP_YIELD, KOS_EV_AWAIT → KOS_SUSP_AWAIT.

---

## Part 7: Throw Injection (INJECT_MODE)

When the caller throws into the generator at a suspended YIELD_FROM, the throw is delivered via `KosContext_INJECT_MODE(ctx) == KOS_INJECT_THROW` with INJECT_VALUE and INJECT_TYPE set. The handler must process this before checking phase. Order of checks:

1. First entry (no node)
2. INJECT_MODE == KOS_INJECT_THROW
3. PHASE == KOS_YF_PHASE_EXPECT_SEND
4. PHASE == KOS_YF_PHASE_EXPECT_AWAIT
5. Default drive

On INJECT_THROW: consume inject, set PENDING_ACTION = KOS_YF_THROW, PENDING_ARG = exc, PENDING_ARG_TYPE = exc_type, PHASE = DRIVING, then drive.

---

## Part 8: Delegate Drive Behavior (Reference)

No functional changes to drive logic. Summary for implementation clarity:

### SYNC_ITER

- Step: `PyIter_Next(delegate)`. Value → push, emit YIELD, set PHASE=EXPECT_SEND. Exhaustion → complete with StopIteration value.
- Send: only None allowed.
- Throw: clear PENDING_ARG, set action=STEP, retry step.
- Close: no-op.

### SYNC_GEN

- Step/Send: `delegate.send(send_val)` (Py_None for step). Value → push, emit YIELD, set PHASE=EXPECT_SEND. StopIteration → complete with value.
- Throw: `delegate.throw(exc)`. Handle result or propagate.
- Close: `delegate.close()`, pop node, set GeneratorExit.

### PY_ASYNC_ITER / PY_ASYNC_GEN

- Resuming from await: INFLIGHT set, PENDING_ARG = await result. Push result, emit YIELD, set PHASE=EXPECT_SEND. (Drive handles this.)
- Step: get_await_next, eager_step_awaitable. Suspended → set INFLIGHT, emit AWAIT, set PHASE=EXPECT_AWAIT. Immediate result → push, emit YIELD, set PHASE=EXPECT_SEND.
- Send: asend(send_val), get awaitable, drive.
- Throw: athrow, drive.
- Close: aclose or treat PY_ASYNC_ITER as exhausted with None.
- StopAsyncIteration: complete with value.

### KOS_ASYNC_GEN

- Resuming from await: PENDING_ARG = await result. Pass as send_val to koskari_eval.
- Step: koskari_eval(agen->ctx, &ev, send_val, ...). KOS_EV_YIELD → push, emit YIELD, set PHASE=EXPECT_SEND. KOS_EV_AWAIT → set INFLIGHT = payload, emit AWAIT, set PHASE=EXPECT_AWAIT. KOS_EV_RETURN → complete with value.
- Send/Throw: forward to koskari_eval.
- Close: set agen->closed, pop node, set GeneratorExit.

---

## Part 9: Clone and Traverse

The new `phase` field is a small integer. Clone copies it. Traverse does not visit it (no Python references). Add to clone:

```c
KosDynYieldFrom_PHASE(clone) = KosDynYieldFrom_PHASE(node);
```

---

## Part 10: Edge Cases and Invariants

### Empty send_val

When the driver resumes with `send_val == NULL` (e.g. first `__anext__` after creation, or coroutine send(None)):

- revive_yield / revive_await do not push when send_val is NULL.
- For PHASE_EXPECT_SEND: if nothing was pushed, TOS is the stale yielded value. We must still pop to clean up. Pop once (the stale value). Use Py_None as PENDING_ARG for the delegate.
- For PHASE_EXPECT_AWAIT: if nothing was pushed, that's an error — we should have an await result. Treat as "resume with None" for robustness: don't pop; set PENDING_ARG = Py_None and proceed. (The driver for async gen typically passes the result. If not, Py_None is a reasonable fallback for "no value".)

**Decision**: For EXPECT_SEND with no inject (stack only has the stale yielded value): pop it (discard), set PENDING_ARG = Py_None, PENDING_ACTION = KOS_YF_SEND. For EXPECT_AWAIT with no inject: do not pop; set PENDING_ARG = Py_None. Proceed to drive. The delegate receives None.

### Nested yield-from

Outer YIELD_FROM delegates to a generator that contains YIELD_FROM. Each has its own node. The outer node is above the inner in the chain. When the inner yields, we emit KOS_EV_YIELD. The outer's delegate (inner gen) is driven by koskari_eval for KOS_ASYNC_GEN, or by send() for SYNC_GEN. No special handling. Each node manages its own phase.

### Continuation capture

The node is on the dynamic chain. Clone copies phase, delegate, kind, pending_*, inflight. On resumption of a captured continuation, the node's phase indicates whether we expect send or await. The inject will be pushed by the driver that invokes the continuation. Same stack-based consumption. No changes to continuation handling.

### Exhaustion during throw

If the delegate handles throw(StopAsyncIteration) and returns a final value, we complete with that value. If it propagates, we propagate. Documented in existing semantics.

### Close propagation

Close flows: caller closes → we get INJECT_MODE or a close signal. We set PENDING_ACTION = KOS_YF_CLOSE and drive. Delegate's close()/aclose() is invoked. We pop the node and set GeneratorExit. Unchanged.

### Koskari async gen without context

KOS_ASYNC_GEN may have agen->ctx == NULL (lazy context). The drive creates ctx on first use. Unchanged.

---

## Part 11: Implementation Order

1. Add `phase` to KosDynYieldFrom in dynchain.h and dynchain.c (alloc, free, clone, traverse).
2. Set phase in kos_dynyieldfrom_new to KOS_YF_PHASE_DRIVING.
3. In YIELD_FROM handler:
   - Add PHASE_EXPECT_SEND and PHASE_EXPECT_AWAIT handling.
   - Add INJECT_THROW consumption (already present; ensure it runs before phase checks).
   - Remove PY_RESUME_KIND consumption.
4. In each drive path that emits YIELD: set PHASE = KOS_YF_PHASE_EXPECT_SEND before return.
5. In each drive path that emits AWAIT: set PHASE = KOS_YF_PHASE_EXPECT_AWAIT before return.
6. In eval.c: remove KOS_SUSP_YIELD_FROM, revive_yield_from, raise_yield_from, and YIELD_FROM cases from eval_phase_entry.
7. Run existing yield-from and demand-sync tests. Add tests for send propagation through yield-from.

---

## Part 12: Testing Requirements

- Yield-from with sync gen: send(value) propagates to delegate.
- Yield-from with async gen: asend(value) propagates.
- Yield-from with Koskari async gen: send propagates via koskari_eval.
- Throw into suspended yield-from: exception reaches delegate.
- Close propagation.
- Nested yield-from: values and send flow correctly.
- Continuation capture and resume with send value.

---

## Part 13: Relation to Existing Documents

This document revises and extends `yield-from-execution-model.md`. The high-level semantics, delegate classification, and delegation rules remain. The changes are:

- Resume inject: stack-based, consumed by the node via phase.
- No SUSP_YIELD_FROM or revive_yield_from.
- Explicit phase enum in the node.
- Stale yielded value cleanup on EXPECT_SEND resume.

---

## The end.
