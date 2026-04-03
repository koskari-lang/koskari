# Koskari Evaluator: Operation and Control Flow

This document describes the operation of the koskari eval loop in `koskari/lib/eval.c`: its stages, how opcodes are invoked and signal back, exception handling, control transit (event-driven handle/unwind model), transfer plans, tail calls, call installs, and resumption paths.

Implementation reference: `koskari/lib/eval.c`, `koskari/lib/eval.h`, `koskari/lib/dynchain.h`, `design/interpreter/evaluator.md`, and `design/interpreter/dynnode-event-model.md`.

---

## 1. High-Level Structure

### 1.1 Entry Point

```c
int koskari_eval(KosContext *ctx, KosEvent *out_ev,
                 PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);
```

The evaluator runs until it hits a **boundary event** (return, raise, yield, await) or a fatal error. It returns:
- **0**: Normal exit; `out_ev` contains the boundary event.
- **-1**: Fatal internal error; `PyErr` is set.

`send_val` and `throw_typ`/`throw_exc` are the resume arguments (both may be NULL for a fresh start or null resume).

### 1.2 Main Loop Phases

```mermaid
flowchart TD
    subgraph koskari_eval["koskari_eval"]
        P1["Phase 1: eval_phase_entry<br/>Resolve mode from suspension state + send_val/throw_exc"]
        mode_check{"mode?"}
        P2["Phase 2: eval_phase_transfer<br/>Drain active transfer plan; terminal RETURN if empty"]
        loop_start["for (;;) dispatch loop"]

        subgraph loop["Loop body"]
            Xfer["run_transfer_plan_to_completion<br/>(if plan active)"]
            P3["Phase 3: eval_phase_pending_exception<br/>PyErr_Occurred? → process_raise"]
            Native{"Frame has native_handler?"}
            P4["Phase 4: eval_phase_prepare_dispatch<br>fetch code, pc, insn; pre-advance pc"]
            P5["Phase 5: opcode dispatch<br/>computed goto → op_xxx(ctx, out_ev, insn)"]
            P6["Phase 6: eval_phase_post_opcode<br>op_res != 0 handling"]
            next["next_insn"]
        end

        P1 --> mode_check
        mode_check -->|"EXCEPTION"| process_raise_1["process_raise → STOP_EVENT or CONTINUE"]
        mode_check -->|"RAISE"| fill_out_ev["fill out_ev, STOP_EVENT"]
        mode_check -->|"else"| P2
        P2 --> loop_start
        loop_start --> Xfer --> P3 --> Native
        Native -->|yes| run_native["run native handler as frame evaluator"]
        Native -->|no| P4
        run_native --> next
        P4 --> P5 --> P6 --> next
        next --> loop_start
    end
```

---

## 2. Phase Detail Diagrams

### 2.1 Phase 1: Entry and Revive

**eval_phase_entry** chooses the initial `KosEvalMode`:

```mermaid
flowchart TD
    entry["eval_phase_entry"]
    plan_check{"plan active?"}
    send_check{"send/throw<br/>NULL?"}
    revive_plan["revive_plan()<br/>→ EXCEPTION / TRANSFER / NORMAL"]
    raise_xyz["raise_await / raise_yield<br/>resume w/o value"]
    revive_xyz["revive_await / revive_yield<br/>→ EXCEPTION or NORMAL"]

    entry --> plan_check
    plan_check -->|yes| revive_plan
    plan_check -->|no| send_check
    send_check -->|yes| raise_xyz
    send_check -->|no| revive_xyz
```

**Revive paths:**
- **revive_plan**: Resume a suspended transfer plan. If `throw_exc` set → `KOS_MODE_EXCEPTION`. Else inject `send_val` into plan → `KOS_MODE_TRANSFER`.
- **revive_await / revive_yield**: If `throw_exc` and `revive_yield` → `PyErr_SetObject`, `KOS_MODE_EXCEPTION`. If `throw_exc` and `revive_await` with `py_await_iter` set → `iterator.throw(typ, val, tb)` first (same idea as `coroutine_throw` when waiting on a Python await iterator): on `StopIteration`, the await completes and the normal inject/send path runs; on non-NULL `throw` result, `out_ev` is `KOS_EV_AWAIT` again and `eval_phase_entry` returns `STOP_EVENT` with `KOS_MODE_AWAIT` (re-suspend); if there is no iterator, or `throw` leaves another error, `KOS_MODE_EXCEPTION`. If no `throw_exc`, `revive_await` pushes `send_val` / `py_resume_inject` onto stack (if non-NULL), then `KOS_MODE_NORMAL`. YIELD_FROM uses these same revive paths (its phase selects AWAIT or YIELD); there is no separate `revive_yield_from` or `KOS_SUSP_YIELD_FROM`.
  - From the caller's perspective, both plain `YIELD` and `YIELD_FROM` suspensions surface as the same boundary kinds (`KOS_EV_YIELD` or `KOS_EV_AWAIT`), so resume routing is identical.

**Mode dispatch outcomes:**
- `KOS_MODE_AWAIT` (re-suspend after `throw` into `py_await_iter` yielded again) → `SUSPENSION_EVENT = AWAIT`, clear `KOS_CTX_RUNNING`, `KOS_STEP_STOP_EVENT` (same suspend shape as opcode AWAIT).
- `KOS_MODE_EXCEPTION` → `try_local_exception_handler`; on success → `KOS_STEP_CONTINUE` or `KOS_STEP_STOP_EVENT` (AWAIT); on failure → `out_ev = KOS_EV_RAISE`, `KOS_STEP_STOP_EVENT`.
- `KOS_MODE_RAISE` → fetch exception, decorate, fill `out_ev`, `KOS_STEP_STOP_EVENT`.

### 2.2 Phase 2: Transfer Plan Drain

**eval_phase_transfer** runs when `KosContext_PLAN(ctx)` is set:

```mermaid
flowchart TD
    start["run_transfer_plan_to_completion(ctx, out_ev)"]
    has_plan{"plan?"}
    step["plan_res = kos_run_transfer_step(ctx, out_ev)"]
    step_check{"plan_res?"}
    fail["plan_res < 0<br/>KOS_EV_RAISE, inject traceback<br/>return -1"]
    suspend["plan_res == KOS_XFER_SUSPENDED_AWAIT<br/>return 0 (plan stays)"]
    next_step["next step"]
    clear["clear_and_free_transfer_plan(ctx)"]
    frame_check{"CURRENT_FRAME(ctx)?"}
    ret_none["KOS_EV_RETURN(None)<br/>STOP_EVENT"]

    start --> has_plan
    has_plan -->|yes| step
    step --> step_check
    step_check -->|< 0| fail
    step_check -->|SUSPENDED_AWAIT| suspend
    step_check -->|1| clear
    step_check -->|0| next_step
    next_step --> has_plan
    has_plan -->|no| clear
    clear --> frame_check
    frame_check -->|NULL| ret_none
    frame_check -->|non-NULL| loop_next["continue to dispatch"]
```

### 2.3 Phase 3: Pending Exception Check

At each loop top, **eval_phase_pending_exception** handles `PyErr_Occurred()` (e.g. from a signal handler or async exception):

```mermaid
flowchart TD
    pyerr{"PyErr_Occurred()?"}
    cont["KOS_STEP_CONTINUE"]
    process["process_raise(ctx, out_ev)"]
    raise_check{"result?"}
    stop_raise["KOS_MODE_RAISE<br/>→ KOS_STEP_STOP_EVENT"]
    stop_await["KOS_EV_AWAIT<br/>SUSPENSION_EVENT = AWAIT<br/>→ KOS_STEP_STOP_EVENT"]
    loop_cont["*loop_continue = 1<br/>KOS_STEP_CONTINUE"]

    pyerr -->|No| cont
    pyerr -->|Yes| process
    process --> raise_check
    raise_check -->|KOS_MODE_RAISE| stop_raise
    raise_check -->|KOS_EV_AWAIT| stop_await
    raise_check -->|else| loop_cont
```

### 2.4 Phase 4: Prepare Dispatch

**eval_phase_prepare_dispatch** fills `KosDispatchCtx`:

1. Check `CURRENT_FRAME(ctx)`.
2. Read `code`, `pc` from frame; bounds-check `pc`.
3. Load `insn = code->code[pc]`.
4. Set `acting_pc = pc` (instruction being executed).
5. Advance `pc` (pre-advance so normal path does not need to redo).

PC is rolled back for WITH enter/exit suspend so the same instruction is retried on resume.

### 2.5 Phase 5: Opcode Dispatch

```mermaid
flowchart LR
    dispatch["optable[op] → op_XXX(ctx, out_ev, insn)"]
    op_res["op_res = handler return value"]
    zero{"op_res == 0?"}
    fast["goto next_insn<br/>(fast path)"]
    post["eval_phase_post_opcode"]

    dispatch --> op_res --> zero
    zero -->|yes| fast
    zero -->|no| post
```

### 2.6 Phase 6: Post-Opcode Handling

**eval_phase_post_opcode** runs when an opcode returns non-zero:

```mermaid
flowchart TD
    control{"op_res == KOS_OP_RES_CONTROL_RETURNED?"}
    ctrl_returned["kos_dynnode_on_control_returned<br/>(wind thunks)<br/>loop_continue = 1, CONTINUE"]
    pyerr{"PyErr_Occurred()?"}
    process["process_raise<br/>→ STOP_EVENT or loop_continue"]
    suspend{"op_res == 1<br/>(suspend)?"}
    with_rollback["WITH rollback? (PUSH_WITH/POP_WITH/etc.)<br/>roll back pc for resume"]
    yield_walk["KOS_EV_YIELD?<br/>kos_dyn_walk_handle_yield<br/>(GEN consumes, freeze)"]
    tag["Tag suspension:<br/>AWAIT → SUSPENSION_EVENT<br/>YIELD → SUSPENSION_EVENT"]

    control -->|Yes| ctrl_returned
    control -->|No| pyerr
    pyerr -->|Yes| process
    pyerr -->|No| suspend
    suspend -->|Yes| with_rollback
    suspend -->|No| yield_walk
    yield_walk --> tag
```

For **YIELD** and **YIELD_FROM** (op_res == 1, `out_ev` == KOS_EV_YIELD): before tagging suspension, the evaluator calls `kos_dyn_walk_handle_yield(ctx, out_ev)`. The dynamic chain is walked from dyn_top toward the frame; each node is offered handle(YIELD). The **generator node** (KOS_DYN_GEN) **consumes** the event and freezes the chain (e.g. `kos_dyn_gen_freeze_and_release`).

Whether `koskari_eval()` actually *returns* this yield to its caller depends on flattening mode:
- **Flattened koskati-to-koskari (NEXT/ITER_NEXT over `KosPyAsyncGen`)**: if `KosContext_PENDING_AGEN(ctx)` is set, `eval_phase_post_opcode` treats the yielded payload as an internal step result. After `kos_dyn_walk_handle_yield`, it pushes the payload onto the current frame stack, clears `out_ev` back to `KOS_EV_NONE` (and drops the payload), and continues the dispatch loop (`*loop_continue = 1`, `KOS_STEP_CONTINUE`). In this mode, the caller never observes a suspension boundary.
- **Regular python-to-koskari boundary**: if `KosContext_PENDING_AGEN(ctx)` is not set, `eval_phase_post_opcode` sets `SUSPENSION_EVENT = KOS_SUSP_YIELD` and exits with STOP_EVENT. The caller observes `out_ev.kind == KOS_EV_YIELD` and later resumes the generator via `revive_yield(send_val)`.

In both cases (plain `YIELD` or `YIELD_FROM`), resume inject uses the same `revive_yield` path when the driver does resume the evaluator.

For **RETURN** that reaches top-level or GEN: the RETURN opcode itself performs `kos_dyn_walk_return_to_frame` (handle phase; FINALLY may consume and run). It then sets `out_ev = KOS_EV_RETURN` and returns 1. Post-opcode does not run a separate walk for RETURN; the boundary is emitted by the opcode.

---

## 3. Opcode Signaling

```mermaid
flowchart TD
    subgraph opcode_return["Opcode handler returns"]
        res{"op_res"}
        r0["0: continue<br/>goto next_insn (fast path)"]
        r1["1: suspend<br/>out_ev set (YIELD/AWAIT)<br/>→ post_opcode"]
        r2["2: KOS_OP_RES_CONTROL_RETURNED<br/>wind thunks, etc.<br/>→ post_opcode"]
        rneg["-1: error<br/>PyErr set<br/>→ post_opcode → process_raise"]
    end
    res --> r0
    res --> r1
    res --> r2
    res --> rneg
```

### 3.1 Opcode Handler Return Values

| Value | Meaning |
|-------|---------|
| **0** | Normal; continue to next instruction. Loop goes to `next_insn` directly. |
| **1** | Suspend (e.g. YIELD, AWAIT, WITH async enter/exit). `out_ev` is set. Loop exits or continues as determined by post-opcode. |
| **2** | `KOS_OP_RES_CONTROL_RETURNED` — control returned to an awaiting dynamic node (e.g. wind thunk). Triggers `kos_dynnode_on_control_returned`; loop continues. |
| **-1** | Error; `PyErr` set. Post-opcode calls `process_raise`; if no local handler, STOP_EVENT with RAISE. |

### 3.2 What Opcodes Can Signal

- **Continue** (0): No boundary; next instruction runs. RETURN and YIELD use this when delivering a value to a parent frame (e.g. returning from a nested CALL, or yielding inside a nested call within a generator).
- **Suspend** (1): `out_ev` holds `KOS_EV_YIELD` or `KOS_EV_AWAIT`. Loop exits to caller. YIELD returns 1 only when the frame's parent is the generator node (KOS_DYN_GEN) or there is no parent; otherwise YIELD detaches and pushes onto the parent and returns 0.
- **Control returned** (2): Used by RETURN and TAIL_CALL when control returns through wind/unwind thunks. RETURN returns `KOS_OP_RES_CONTROL_RETURNED` when it delivers a value to a non-generator parent (after `kos_dyn_walk_return_to_frame`, detach, push value onto parent stack).
- **Exception** (-1): Opcode or callee sets `PyErr`; post-opcode runs `process_raise`.

### 3.3 Boundary Events and Parent Frame

Boundary events occur when the current frame has **no parent** or its parent is the **generator node** (KOS_DYN_GEN):

- **RETURN** → `KOS_EV_RETURN`, payload = value (top-level or parent is GEN)
- **RAISE**  → `KOS_EV_RAISE`, payload = exception value
- **YIELD**  → `KOS_EV_YIELD`, payload = yielded value (parent is GEN or no parent)
- **AWAIT**  → `KOS_EV_AWAIT`, payload = awaitable

When the frame **has a parent that is not the generator node** (e.g. normal CALL, or async gen stepping): YIELD and RETURN do **not** produce boundary events. They walk the dynamic chain (handle phase), detach the frame, push the value onto the parent stack, and return 0 (YIELD) or `KOS_OP_RES_CONTROL_RETURNED` (RETURN) so the loop continues in the caller. DELIMITED return (RETURN_TARGET_CHAIN set) is handled separately and may build a transfer plan.

---

## 4. Exception Handling

### 4.1 Exception Flow

```mermaid
flowchart TD
    exc["Exception occurs (PyErr set)"]
    process["process_raise(ctx, out_ev)"]
    cc_check{"KoskariContinuationApply?"}
    handle_cc["handle_continuation_apply"]
    own{"Own token?"}
    invoke["kos_invoke_continuation<br/>KOS_MODE_TRANSFER"]
    foreign["kos_build_cc_unwind_plan<br/>KOS_MODE_TRANSFER"]
    try_local["try_local_exception_handler(ctx, out_ev)"]
    region{"region found<br/>TRY/FINALLY?"}
    jump["jump to handler, return 1"]
    pop["pop nodes until frame or BARRIER"]
    barrier{"BARRIER +<br/>HANDLES_EXCEPTIONS?"}
    unwind["kos_dyn_exception_unwind"]
    swallowed{"pop swallowed?"}
    no_handler["out_ev = KOS_EV_RAISE<br/>return 0"]

    exc --> process
    process --> cc_check
    cc_check -->|yes| handle_cc
    handle_cc --> own
    own -->|yes| invoke
    own -->|no| foreign
    cc_check -->|no| try_local
    try_local --> region
    region -->|yes| jump
    region -->|no| pop
    pop --> barrier
    barrier -->|yes| unwind
    barrier -->|no| swallowed
    swallowed -->|yes| loop_cont2["continue loop"]
    swallowed -->|no| no_handler
```

### 4.2 try_local_exception_handler

1. `kos_find_node_for_raise(ctx)` — topmost enabled TRY or FINALLY.
2. If found: clear stack block, set PC to handler, stash exception in `PyErr_SetExcInfo`, clear PyErr, set `handling_raise` on FINALLY, return 1.
3. If not found: walk chain from `dyn_top` toward frame. For each node:
   - `kos_dyn_pop_top`; if `KOS_DYN_UNWIND_ASYNC_STARTED`, return 1.
   - If pop fails (BARRIER): dispatch to `kos_dyn_exception_unwind` if node has HANDLES_EXCEPTIONS.
   - If node may-swallow and PyErr cleared: set PC to catch_resume_pc, return 1.
4. If no handler: `inject_eval_frame_traceback`, fill `out_ev` with KOS_EV_RAISE, return 0.

### 4.3 Traceback Injection

`inject_eval_frame_traceback` decorates the exception with `__koskari_dynchain__` and injects the current frame into the traceback so Python can display Koskari-native frames. Called at:
- `try_local_exception_handler` (handle and re-raise branches)
- `KOS_MODE_RAISE` at entry
- `run_transfer_plan_to_completion` when a transfer step fails

---

## 5. Control Transit (Event-Driven Model)

Control transits (RETURN, RAISE, YIELD, BREAK, CONTINUE, SHIFT) use a two-phase model: **handle()** then **unwind()**. Nodes are offered the event via `kos_dyn_handle` before being unwound. See `design/interpreter/dynnode-event-model.md` for the full design.

### 5.1 handle(node, event)

`kos_dyn_handle(ctx, node, event)` offers a control transit to each node. The node may:
- **PASS** — pass through unchanged
- **CONSUME** — handle the event (e.g. TRY catches RAISE, FINALLY consumes RETURN)
- **DENY** — reject (e.g. generator boundary denies SHIFT)

### 5.2 unwind(node, event)

`kos_dyn_unwind` takes a `KosXferEvent` (RETURN, RAISE, YIELD, BREAK, CONTINUE, SHIFT) so nodes know *why* they are being torn down. Transfer-aware nodes (e.g. `dynamic-wind`) use this for after-thunks.

### 5.3 Transit Types

| Transit | Walk | Key functions |
|---------|------|----------------|
| RETURN | handle(RETURN) per node; FINALLY may overwrite value | `kos_dyn_walk_return_to_frame` |
| RAISE | handle(RAISE); TRY/FINALLY catch | `process_raise` → `try_local_exception_handler`; `kos_dyn_handle` on unwind path |
| YIELD | handle(YIELD) per node; generator boundary intercepts | `kos_dyn_walk_handle_yield` |
| BREAK / CONTINUE | transfer_to_loop; unwind(node, event); handle on loop | `kos_dyn_transfer_to_loop` |
| SHIFT | handle(SHIFT); prompt consumes; generator boundary denies | `kos_dyn_walk_handle_shift_to_prompt` |

### 5.4 Generator Boundary Node

`KOS_DYN_GEN` marks the generator boundary. It is pushed when a generator or coroutine is **installed** (e.g. when Python drives a Koskari generator via `send`/`throw`); the frame's parent is then the GEN node.

- **handle(YIELD)**: GEN **consumes** the event. The evaluator calls `kos_dyn_walk_handle_yield` after an opcode (plain `YIELD` or `YIELD_FROM`) sets `out_ev.kind = KOS_EV_YIELD`. GEN's handler runs (e.g. `kos_dyn_gen_freeze_and_release`): the current dynchain is captured/frozen. Resumption is via `revive_yield`, which pushes `send_val` onto the stack and continues at the next instruction.

Whether control returns to the caller depends on evaluator flattening:
- In the regular case, `eval_phase_post_opcode` sets `SUSPENSION_EVENT = KOS_SUSP_YIELD` and returns STOP_EVENT; the caller resumes via `revive_yield`.
- In flattened async-gen stepping (`KosContext_PENDING_AGEN(ctx)`), `eval_phase_post_opcode` clears `out_ev` and continues dispatch; the yield does not escape as a boundary event.
- **handle(SHIFT)**: GEN **denies**; shift cannot cross the generator boundary.
- **RETURN** when the frame's parent is GEN (or there is no parent) produces a boundary event `KOS_EV_RETURN` and the evaluator exits; the driver receives the return value (e.g. StopIteration for sync generators).

---

## 6. Transfer Plans

### 6.1 Transfer Plan Overview

A **KosTransferPlan** is a list of actions that orchestrate continuation invocation, delimited return, or foreign-token unwind. The loop drains it via `run_transfer_plan_to_completion` → `kos_run_transfer_step`.

```mermaid
flowchart LR
    subgraph steps["Transfer step types"]
        EXTENT["EXTENT_ACTION<br/>wind/unwind"]
        INSTALL["INSTALL<br/>splice chain"]
        INJECT["INJECT<br/>push value"]
        POP["POP_ONE<br/>unwind top"]
        PUSH["PUSH<br/>graft segment"]
        TRUNCATE["TRUNCATE_AND_INJECT<br/>free + push"]
    end
    plan["KosTransferPlan"] --> steps
```

### 6.2 Transfer Step Types

| Step | Description |
|------|-------------|
| `KOS_XFER_EXTENT_ACTION` | Run wind (enter) or unwind (exit) for a node. |
| `KOS_XFER_INSTALL` | Splice a chain segment as the new current chain. |
| `KOS_XFER_INJECT` | Push resume value onto stack (or throw). |
| `KOS_XFER_POP_ONE` | Unwind current top, unlink and free. |
| `KOS_XFER_PUSH` | Append segment to current chain (graft). Used for delimited invoke. |
| `KOS_XFER_TRUNCATE_AND_INJECT` | Free from top through node, set dyn_top to parent, push value. |

### 6.3 kos_run_transfer_step Return Values

| Value | Meaning |
|-------|---------|
| **0** | Step complete; continue to next step. |
| **1** | Plan complete; plan is cleared. |
| **KOS_XFER_SUSPENDED_AWAIT** (2) | Step suspended on await; plan stays; driver resumes and feeds inject. |
| **< 0** | Error; PyErr set; plan fails, `run_transfer_plan_to_completion` fills out_ev with KOS_EV_RAISE. |

### 6.4 Call Sites for run_transfer_plan_to_completion

1. **eval_phase_transfer** — at loop start when plan is active.
2. **Inside dispatch loop** — at each iteration top before phase 3.
3. **eval_phase_entry** — when re-entering with an active plan (e.g. after yield-from or boundary return).

---

## 7. Tail Calls and Call Installs

### 7.1 Invocation Entry Points

- `kos_callable_call` — non-tail: look up `tp_kos_install`, invoke.
- `kos_callable_tail_call` — tail: look up `tp_kos_tail_install`; if NULL, push empty frame + `tp_kos_install`.

### 7.2 Install Slot Contract

```c
int (*KosInstallFunc)(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
```

- **0**: Success; evaluator continues with new/reset frame.
- **1**: Suspend; `out_ev` set (e.g. AWAIT).
- **-1**: Error; `PyErr` set.

The install owns all frame setup: push new frame (non-tail) or reset current frame (tail). It does **not** recurse into `koskari_eval`; execution continues in the same loop.

### 7.3 Tail Call Behavior

For `kos_callable_tail_call`:
- If `tp_kos_tail_install` exists: it resets the current frame (code, ns, stack, pc) and returns 0.
- If not: push `kos_dyn_push_frame_empty`, then call `tp_kos_install` (no TCO).

TAIL_CALL opcodes return `KOS_OP_RES_CONTROL_RETURNED` when they install a new frame; `eval_phase_post_opcode` invokes `kos_dynnode_on_control_returned` for wind thunks.

### 7.4 Transfer Plan Integration

An install that invokes a continuation builds a transfer plan and sets `KosContext_PLAN(ctx) = plan`. The eval loop drains it on the next iteration. No recursion.

---

## 8. Resume Paths

```mermaid
flowchart LR
    subgraph events["SUSPENSION_EVENT"]
        AWAIT
        YIELD
        NONE
    end
    subgraph revives["Revive function"]
        revive_await["revive_await"]
        revive_yield["revive_yield"]
        no_revive["—"]
    end
    AWAIT --> revive_await
    YIELD --> revive_yield
    NONE --> no_revive
```

### 8.1 Suspension Event → Revive

| `SUSPENSION_EVENT` | Revive function | Behavior |
|--------------------|-----------------|----------|
| `KOS_SUSP_AWAIT` | `revive_await` | Push send_val, complete pending WITH exit if applicable. |
| `KOS_SUSP_YIELD` | `revive_yield` | Push send_val onto stack. |
| `KOS_SUSP_NONE` | — | Fresh start or no-op resume. |

YIELD_FROM emits only YIELD or AWAIT; its phase (KosYfPhase) selects which. There is no `KOS_SUSP_YIELD_FROM` or `revive_yield_from`.

If `throw_typ`/`throw_exc` are set, any revive path calls `PyErr_SetObject` and yields `KOS_MODE_EXCEPTION`, which triggers `process_raise` at entry.

### 8.2 Native Frame Handler

When `CURRENT_FRAME(ctx)` has `native_handler != NULL`, the loop runs that handler as the frame evaluator instead of bytecode dispatch. On resume with `send_val`, entry delivers the value to the handler via `fn(ctx, out_ev, 1, val)`.

---

## 9. Play-by-Play Scenarios

### 9.1 Scenario: Simple Call and Return

1. Entry: `send_val == NULL`, `SUSPENSION_EVENT == NONE` → `KOS_MODE_NORMAL`.
2. Transfer: no plan → skip.
3. Pending exception: none.
4. Dispatch: opcode is CALL. Handler calls `kos_callable_call`, which invokes install. Install pushes new frame, returns 0.
5. `op_res == 0` → goto `next_insn`. New frame is now top; next opcode runs in callee.
6. Eventually RETURN in callee. Frame has parent → detach frame, push value onto parent, return 0. Loop continues in caller.
7. Caller runs until top-level RETURN → `out_ev = KOS_EV_RETURN`, return 1 from opcode → STOP_EVENT.

### 9.2 Scenario: Exception and Local Handler

1. Opcode fails, sets PyErr. `op_res == -1`.
2. Post-opcode: `PyErr_Occurred()` → `process_raise`.
3. `try_local_exception_handler` finds TRY region, sets handler_pc, clears PyErr for handler, returns 1.
4. `process_raise` returns `KOS_MODE_NORMAL`. Post-opcode sets `loop_continue = 1`, returns CONTINUE.
5. Loop continues; next instruction is the except block.

### 9.3 Scenario: Continuation Invoke (Own Token)

1. Python calls `cont(value)`. Cont raises `KoskariContinuationApply` targeting this context.
2. `PyErr_Occurred()` at next loop top (or after opcode). `process_raise` runs.
3. `handle_continuation_apply` sees own token: fetches continuation and value, clears PyErr, calls `kos_invoke_continuation`.
4. `kos_invoke_continuation` builds transfer plan, sets `KosContext_PLAN(ctx) = plan`, returns 0. `handle_continuation_apply` returns `KOS_MODE_TRANSFER`.
5. Loop continues (or entry yields TRANSFER). `run_transfer_plan_to_completion` drains plan: unwind extents, install captured chain, inject value.
6. Execution resumes at captured PC with value on stack.

### 9.4 Scenario: AWAIT Suspend and Resume

1. AWAIT opcode yields awaitable. Sets `out_ev = KOS_EV_AWAIT`, `py_await_iter = iter`, returns 1.
2. Post-opcode tags `SUSPENSION_EVENT = AWAIT`, returns STOP_EVENT. Evaluator exits.
3. Driver (e.g. KosCoroutine.send) awaits the awaitable, gets result.
4. Driver calls `koskari_eval(ctx, &ev, result, NULL, NULL)`.
5. Entry: `SUSPENSION_EVENT == AWAIT`, `send_val != NULL` → `revive_await(result, ...)`.
6. `revive_await` pushes result onto stack. If `PENDING_WITH_EXIT_NODE`, completes pending WITH exit.
7. Mode = NORMAL. Loop continues; next instruction consumes the awaited value.

**Throw resume (`throw_typ` / `throw_exc` non-NULL):** When `py_await_iter` is set, `revive_await` calls `throw` on that iterator before installing `PyErr` for the VM. If the iterator completes (`StopIteration`), the completion value is staged like a normal send resume (inject / stack push). If `throw` returns a new object, `out_ev` is `KOS_EV_AWAIT` again and eval stops (second suspend). If `throw` fails or leaves an exception, `KOS_MODE_EXCEPTION` runs `try_local` as usual. If there is no `py_await_iter`, behavior matches the old path (`PyErr_SetObject` only).

### 9.5 Scenario: YIELD Suspend and Resume

1. Generator is running; frame's parent is GEN. Plain `YIELD` or `YIELD_FROM` sets `out_ev = KOS_EV_YIELD`, sets payload, marks context DEAD, returns 1.
2. Post-opcode: `KosEvent_KIND(out_ev) == KOS_EV_YIELD` → `kos_dyn_walk_handle_yield(ctx, out_ev)`. Chain is walked; GEN receives handle(YIELD) and **consumes** it (e.g. `kos_dyn_gen_freeze_and_release`): dynchain is frozen, context suspended.
3. Post-opcode sets `SUSPENSION_EVENT = KOS_SUSP_YIELD` and returns STOP_EVENT in the regular boundary case. If `KosContext_PENDING_AGEN(ctx)` is set (flattened async-gen stepping), `eval_phase_post_opcode` instead clears `out_ev` and continues dispatch (no external suspension).
4. Driver (e.g. Python generator `send`) receives `KOS_EV_YIELD` and payload in the regular boundary case; later calls `koskari_eval(ctx, &ev, send_val, NULL, NULL)`.
5. Entry: `SUSPENSION_EVENT == YIELD`, `send_val` provided → `revive_yield(send_val, ...)`.
6. `revive_yield` pushes `send_val` onto the stack. Mode = NORMAL. Loop continues; next instruction runs with the sent value on the stack.

### 9.6 Scenario: Tail Call

1. TAIL_CALL opcode calls `kos_callable_tail_call`. Callable has `tp_kos_tail_install`.
2. Install resets current frame (code, ns, stack, pc) for the new function, returns 0.
3. Opcode returns `KOS_OP_RES_CONTROL_RETURNED`.
4. Post-opcode: `kos_dynnode_on_control_returned` runs (wind thunks if any). `loop_continue = 1`, CONTINUE.
5. Loop continues; next instruction is first instruction of the new function (same frame, new content).

### 9.7 Scenario: Transfer Plan Step Fails

1. Plan is draining. `kos_run_transfer_step` returns < 0 (e.g. exception in extent action).
2. `run_transfer_plan_to_completion` fetches PyErr, calls `inject_eval_frame_traceback`, fills `out_ev` with KOS_EV_RAISE, sets context DEAD, returns -1.
3. Caller (eval loop or entry) sees -1; exits with `eval_ret = 0`, `out_ev` already filled.

---

## 10. Summary Table: Modes and Outcomes

| KosEvalMode | Source | Outcome |
|-------------|--------|---------|
| NORMAL | Fresh start, resume w/o throw | Continue to dispatch |
| TRANSFER | revive_plan, handle_continuation_apply | Drain plan, then dispatch |
| EXCEPTION | throw on resume, revive_plan throw | process_raise → handler or RAISE |
| RAISE | Stored KOS_EV_RAISE at entry | Fill out_ev, STOP_EVENT |
| AWAIT / YIELD | (Used internally for resume routing) | — |

| KosEvalStep | Meaning |
|-------------|---------|
| CONTINUE | Proceed to next phase or next iteration |
| STOP_EVENT | out_ev filled; return 0 to caller |
| FATAL_ERROR | PyErr set; return -1 to caller |

---

## 11. Related Documentation

- [design/interpreter/evaluator.md](interpreter/evaluator.md) — VM structures, C API reference
- [design/interpreter/dynnode-event-model.md](interpreter/dynnode-event-model.md) — Event-driven handle/unwind model
- [design/interpreter/ceval-exception-paths.md](interpreter/ceval-exception-paths.md) — Exception paths and trace injection
- [design/api/special-call-protocol.md](api/special-call-protocol.md) — Install slots and invocation
- [design/interpreter/execution-invariants.md](interpreter/execution-invariants.md) — Execution invariants
- [design/interpreter/iteration-yield-boundaries.md](interpreter/iteration-yield-boundaries.md) — Iteration vs yield-from vs for-each: implementation map and consolidation proposals
