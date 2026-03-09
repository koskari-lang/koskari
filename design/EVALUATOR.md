# Koskari Evaluator: Operation and Control Flow

This document describes the operation of the koskari eval loop in `koskari/lib/eval.c`: its stages, how opcodes are invoked and signal back, exception handling, transfer plans, tail calls, call installs, and resumption paths.

Implementation reference: `koskari/lib/eval.c`, `koskari/lib/eval.h`, `koskari/lib/dynchain.h`, and `design/interpreter/evaluator.md`.

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
    raise_xyz["raise_await / raise_yield /<br/>raise_yield_from<br/>resume w/o value"]
    revive_xyz["revive_await / revive_yield /<br/>revive_yield_from<br/>→ EXCEPTION or NORMAL"]

    entry --> plan_check
    plan_check -->|yes| revive_plan
    plan_check -->|no| send_check
    send_check -->|yes| raise_xyz
    send_check -->|no| revive_xyz
```

**Revive paths:**
- **revive_plan**: Resume a suspended transfer plan. If `throw_exc` set → `KOS_MODE_EXCEPTION`. Else inject `send_val` into plan → `KOS_MODE_TRANSFER`.
- **revive_await / revive_yield / revive_yield_from**: If `throw_exc` → `PyErr_SetObject`, `KOS_MODE_EXCEPTION`. Else push `send_val` onto stack (if non-NULL), then `KOS_MODE_NORMAL`.

**Mode dispatch outcomes:**
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
    tag["Tag suspension:<br/>AWAIT → SUSPENSION_EVENT<br/>YIELD → SUSPENSION_EVENT"]

    control -->|Yes| ctrl_returned
    control -->|No| pyerr
    pyerr -->|Yes| process
    pyerr -->|No| suspend
    suspend -->|Yes| with_rollback
    suspend -->|No| tag
```

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

- **Continue** (0): No boundary; next instruction runs.
- **Suspend** (1): `out_ev` holds `KOS_EV_YIELD` or `KOS_EV_AWAIT`. Loop exits to caller.
- **Control returned** (2): Used by RETURN, TAIL_CALL when control returns through wind/unwind thunks.
- **Exception** (-1): Opcode or callee sets `PyErr`; post-opcode runs `process_raise`.

### 3.3 Boundary Events (top-level only)

When the current frame has **no parent** (top-level):

- **RETURN** → `KOS_EV_RETURN`, payload = value
- **RAISE**  → `KOS_EV_RAISE`, payload = exception value
- **YIELD**  → `KOS_EV_YIELD`, payload = yielded value
- **AWAIT**  → `KOS_EV_AWAIT`, payload = awaitable

When the frame **has a parent** (callee or attached async gen): YIELD and RETURN do **not** produce boundary events; they detach the frame, push value onto the parent stack, and return 0 so the loop continues.

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

## 5. Transfer Plans

### 5.1 Transfer Plan Overview

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

### 5.2 Transfer Step Types

| Step | Description |
|------|-------------|
| `KOS_XFER_EXTENT_ACTION` | Run wind (enter) or unwind (exit) for a node. |
| `KOS_XFER_INSTALL` | Splice a chain segment as the new current chain. |
| `KOS_XFER_INJECT` | Push resume value onto stack (or throw). |
| `KOS_XFER_POP_ONE` | Unwind current top, unlink and free. |
| `KOS_XFER_PUSH` | Append segment to current chain (graft). Used for delimited invoke. |
| `KOS_XFER_TRUNCATE_AND_INJECT` | Free from top through node, set dyn_top to parent, push value. |

### 5.3 kos_run_transfer_step Return Values

| Value | Meaning |
|-------|---------|
| **0** | Step complete; continue to next step. |
| **1** | Plan complete; plan is cleared. |
| **KOS_XFER_SUSPENDED_AWAIT** (2) | Step suspended on await; plan stays; driver resumes and feeds inject. |
| **< 0** | Error; PyErr set; plan fails, `run_transfer_plan_to_completion` fills out_ev with KOS_EV_RAISE. |

### 5.4 Call Sites for run_transfer_plan_to_completion

1. **eval_phase_transfer** — at loop start when plan is active.
2. **Inside dispatch loop** — at each iteration top before phase 3.
3. **eval_phase_entry** — when re-entering with an active plan (e.g. after yield-from or boundary return).

---

## 6. Tail Calls and Call Installs

### 6.1 Invocation Entry Points

- `kos_callable_call` — non-tail: look up `tp_kos_install`, invoke.
- `kos_callable_tail_call` — tail: look up `tp_kos_tail_install`; if NULL, push empty frame + `tp_kos_install`.

### 6.2 Install Slot Contract

```c
int (*KosInstallFunc)(KosContext *ctx, KosEvent *out_ev,
                      PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict);
```

- **0**: Success; evaluator continues with new/reset frame.
- **1**: Suspend; `out_ev` set (e.g. AWAIT).
- **-1**: Error; `PyErr` set.

The install owns all frame setup: push new frame (non-tail) or reset current frame (tail). It does **not** recurse into `koskari_eval`; execution continues in the same loop.

### 6.3 Tail Call Behavior

For `kos_callable_tail_call`:
- If `tp_kos_tail_install` exists: it resets the current frame (code, ns, stack, pc) and returns 0.
- If not: push `kos_dyn_push_frame_empty`, then call `tp_kos_install` (no TCO).

TAIL_CALL opcodes return `KOS_OP_RES_CONTROL_RETURNED` when they install a new frame; `eval_phase_post_opcode` invokes `kos_dynnode_on_control_returned` for wind thunks.

### 6.4 Transfer Plan Integration

An install that invokes a continuation builds a transfer plan and sets `KosContext_PLAN(ctx) = plan`. The eval loop drains it on the next iteration. No recursion.

---

## 7. Resume Paths

```mermaid
flowchart LR
    subgraph events["SUSPENSION_EVENT"]
        AWAIT
        YIELD
        YIELD_FROM
        NONE
    end
    subgraph revives["Revive function"]
        revive_await["revive_await"]
        revive_yield["revive_yield"]
        revive_yield_from["revive_yield_from"]
        no_revive["—"]
    end
    AWAIT --> revive_await
    YIELD --> revive_yield
    YIELD_FROM --> revive_yield_from
    NONE --> no_revive
```

### 7.1 Suspension Event → Revive

| `SUSPENSION_EVENT` | Revive function | Behavior |
|--------------------|-----------------|----------|
| `KOS_SUSP_AWAIT` | `revive_await` | Push send_val, complete pending WITH exit if applicable. |
| `KOS_SUSP_YIELD` | `revive_yield` | Push send_val onto stack. |
| `KOS_SUSP_YIELD_FROM` | `revive_yield_from` | Push send_val onto stack. |
| `KOS_SUSP_NONE` | — | Fresh start or no-op resume. |

If `throw_typ`/`throw_exc` are set, any revive path calls `PyErr_SetObject` and yields `KOS_MODE_EXCEPTION`, which triggers `process_raise` at entry.

### 7.2 Native Frame Handler

When `CURRENT_FRAME(ctx)` has `native_handler != NULL`, the loop runs that handler as the frame evaluator instead of bytecode dispatch. On resume with `send_val`, entry delivers the value to the handler via `fn(ctx, out_ev, 1, val)`.

---

## 8. Play-by-Play Scenarios

### 8.1 Scenario: Simple Call and Return

1. Entry: `send_val == NULL`, `SUSPENSION_EVENT == NONE` → `KOS_MODE_NORMAL`.
2. Transfer: no plan → skip.
3. Pending exception: none.
4. Dispatch: opcode is CALL. Handler calls `kos_callable_call`, which invokes install. Install pushes new frame, returns 0.
5. `op_res == 0` → goto `next_insn`. New frame is now top; next opcode runs in callee.
6. Eventually RETURN in callee. Frame has parent → detach frame, push value onto parent, return 0. Loop continues in caller.
7. Caller runs until top-level RETURN → `out_ev = KOS_EV_RETURN`, return 1 from opcode → STOP_EVENT.

### 8.2 Scenario: Exception and Local Handler

1. Opcode fails, sets PyErr. `op_res == -1`.
2. Post-opcode: `PyErr_Occurred()` → `process_raise`.
3. `try_local_exception_handler` finds TRY region, sets handler_pc, clears PyErr for handler, returns 1.
4. `process_raise` returns `KOS_MODE_NORMAL`. Post-opcode sets `loop_continue = 1`, returns CONTINUE.
5. Loop continues; next instruction is the except block.

### 8.3 Scenario: Continuation Invoke (Own Token)

1. Python calls `cont(value)`. Cont raises `KoskariContinuationApply` targeting this context.
2. `PyErr_Occurred()` at next loop top (or after opcode). `process_raise` runs.
3. `handle_continuation_apply` sees own token: fetches continuation and value, clears PyErr, calls `kos_invoke_continuation`.
4. `kos_invoke_continuation` builds transfer plan, sets `KosContext_PLAN(ctx) = plan`, returns 0. `handle_continuation_apply` returns `KOS_MODE_TRANSFER`.
5. Loop continues (or entry yields TRANSFER). `run_transfer_plan_to_completion` drains plan: unwind extents, install captured chain, inject value.
6. Execution resumes at captured PC with value on stack.

### 8.4 Scenario: AWAIT Suspend and Resume

1. AWAIT opcode yields awaitable. Sets `out_ev = KOS_EV_AWAIT`, `py_await_iter = iter`, returns 1.
2. Post-opcode tags `SUSPENSION_EVENT = AWAIT`, returns STOP_EVENT. Evaluator exits.
3. Driver (e.g. KosCoroutine.send) awaits the awaitable, gets result.
4. Driver calls `koskari_eval(ctx, &ev, result, NULL, NULL)`.
5. Entry: `SUSPENSION_EVENT == AWAIT`, `send_val != NULL` → `revive_await(result, ...)`.
6. `revive_await` pushes result onto stack. If `PENDING_WITH_EXIT_NODE`, completes pending WITH exit.
7. Mode = NORMAL. Loop continues; next instruction consumes the awaited value.

### 8.5 Scenario: Tail Call

1. TAIL_CALL opcode calls `kos_callable_tail_call`. Callable has `tp_kos_tail_install`.
2. Install resets current frame (code, ns, stack, pc) for the new function, returns 0.
3. Opcode returns `KOS_OP_RES_CONTROL_RETURNED`.
4. Post-opcode: `kos_dynnode_on_control_returned` runs (wind thunks if any). `loop_continue = 1`, CONTINUE.
5. Loop continues; next instruction is first instruction of the new function (same frame, new content).

### 8.6 Scenario: Transfer Plan Step Fails

1. Plan is draining. `kos_run_transfer_step` returns < 0 (e.g. exception in extent action).
2. `run_transfer_plan_to_completion` fetches PyErr, calls `inject_eval_frame_traceback`, fills `out_ev` with KOS_EV_RAISE, sets context DEAD, returns -1.
3. Caller (eval loop or entry) sees -1; exits with `eval_ret = 0`, `out_ev` already filled.

---

## 9. Summary Table: Modes and Outcomes

| KosEvalMode | Source | Outcome |
|-------------|--------|---------|
| NORMAL | Fresh start, resume w/o throw | Continue to dispatch |
| TRANSFER | revive_plan, handle_continuation_apply | Drain plan, then dispatch |
| EXCEPTION | throw on resume, revive_plan throw | process_raise → handler or RAISE |
| RAISE | Stored KOS_EV_RAISE at entry | Fill out_ev, STOP_EVENT |
| AWAIT / YIELD / YIELD_FROM | (Used internally for resume routing) | — |

| KosEvalStep | Meaning |
|-------------|---------|
| CONTINUE | Proceed to next phase or next iteration |
| STOP_EVENT | out_ev filled; return 0 to caller |
| FATAL_ERROR | PyErr set; return -1 to caller |

---

## 10. Related Documentation

- [design/interpreter/evaluator.md](interpreter/evaluator.md) — VM structures, C API reference
- [design/interpreter/ceval-exception-paths.md](interpreter/ceval-exception-paths.md) — Exception paths and trace injection
- [design/api/special-call-protocol.md](api/special-call-protocol.md) — Install slots and invocation
- [design/interpreter/execution-invariants.md](interpreter/execution-invariants.md) — Execution invariants
