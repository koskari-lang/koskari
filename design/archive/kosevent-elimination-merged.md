# Eval Simplification: KosEvent Elimination (Merged Plan)

**Supersedes:** `kosevent-elimination.md` and
`kosevent-elimination-simplification-response.md`. Those documents remain for
context; this is the normative implementation reference.

**Goal:** Delete `KosEvent` / `KosEventKind` / `KOS_EV_*` entirely. Replace
with `KosOpResult` returned by value throughout. No replacement struct. No
pointer threading. Every function that can produce a boundary signal returns it.
Callers relay or act directly.

Paired goal: leave the eval loop story simple enough to describe in one
paragraph — one `apply_kos_op_result` function routes all non-`NORMAL` results;
`NORMAL` is the hot-path tail dispatch.

---

## Completion criteria

```bash
# 1. No KosEvent anywhere in library C sources
grep -rn 'KosEvent\|KOS_EV_\b' koskari/lib/ --include='*.c' --include='*.h'
# must return nothing

# 2. RETURN.c owns only local work
grep -n 'KOS_TRANS_WALK_RETURN_TO_FRAME' koskari/lib/opcode/RETURN.c
# must be empty or inside a delimited-path helper only

# 3. Test suite green after each step
tox -qe test
```

---

## What gets deleted

| Item | Location |
|------|----------|
| `KosEvent` struct, `KosEventKind` enum | `eval.h` |
| `KOS_EV_NONE/RETURN/RAISE/YIELD/AWAIT` constants | `eval.h` |
| `KosEvent_KIND`, `KosEvent_PAYLOAD`, `KosEvent_EXC_TYPE` macros | `eval.h` |
| `struct KosEvent` forward declarations | `lib.h`, `callable.h` |
| `kos_eval_emit_raise_from_pyerr` | `eval.c`, `eval.h` |
| `kos_op_result_from_int_step`, `kos_op_result_from_call_int` | `eval_utils.c` |
| `KOS_OP_RES_CONTROL_RETURNED`, `KOS_OP_RES_PARENT_RETURN_DELIVER` defines | `eval.h` |
| `KOS_DYN_UNWIND_ASYNC_STARTED` as a return value (absorbed) | `dynchain.h` |
| `KosEvalResult` / `KosEvalResultKind` if unused | `eval.h` |
| `eval_dispatch_op_result` | `eval.c` |
| `KOS_EVAL_DISPATCH_STEP` macro | `eval.h` |

---

## Invariants established by this work

### RAISE invariant

For every `KOS_RES_RAISE` result at any call boundary: **`PyErr` is set on the
thread.** No `exc_type` struct field. No fetch-into-struct/restore-in-driver
pattern. Callers see `KOS_RES_RAISE` and propagate `PyErr` as-is.

**Replacement for `kos_eval_emit_raise_from_pyerr`:** A new static helper
`kos_eval_prepare_boundary_raise(KosContext *ctx, KosEvalState *es)`:

1. Flush live registers (`KosEvalState_FlushLiveRegsToFrame` when `es` non-NULL)
2. `PyErr_Fetch` → `inject_eval_frame_traceback` → `PyErr_Restore` (inject
   traceback in-place; leave `PyErr` set)
3. Mark context dead (`kos_eval_mark_context_dead`)

Call sites in `dyn_walk.c` and `eval_chain.c` call this, then return
`EVAL_RAISE()` (or the caller's equivalent).

### Async-unwind caution

`KOS_DYN_UNWIND_ASYNC_STARTED` becomes `EVAL_AWAIT(awaitable)` on paths where
the awaitable flows from the async call directly. On paths where the WITH node
handles resumption internally (pending-phase state machine, `pending_return_value`
staging), the function returns `EVAL_REPROLOGUE()` instead and the node phase
tracks state. Verify against the matrix in the verification section before
merging Step 3.

---

## What stays

- `KosOpResult {KosOpResultKind kind; PyObject *val}` and all `KOS_RES_*` / `EVAL_*`
- `KosEvalStep` / `KOS_STEP_*` for phase-level helpers within `eval.c` that
  carry no value (`eval_phase_entry`, `eval_step_drain_transfer_plan`,
  `eval_step_native_iteration`) — optional Track C cleanup later
- `KosSuspensionKind` / `KosEvalState_SetSuspensionEvent` (context freeze
  markers; separate from event signaling)
- `KosXferEvent` (transfer-plan domain; unchanged)

---

## Changes

### `apply_kos_op_result` — the single apply function

Introduce one `static KosOpResult apply_kos_op_result(KosEvalState *es,
KosOpResult res)` in `eval.c`. It routes every non-`NORMAL` result through
its event handler and returns the terminal result for `koskari_eval`, or
`EVAL_REPROLOGUE()` when the loop should continue.

```c
static KosOpResult apply_kos_op_result(KosEvalState *es, KosOpResult res) {
  switch (res.kind) {
    case KOS_RES_REPROLOGUE:
      return res;
    case KOS_RES_RAISE:
      return eval_event_raise(es);
    case KOS_RES_YIELD:
      return eval_event_yield(es, res.val);
    case KOS_RES_AWAIT:
      return eval_event_await(es, res.val);
    case KOS_RES_RETURN:
      return eval_event_return(es, res.val);
    case KOS_RES_PARENT_RETURN:
      return eval_parent_return_deliver(es, res.val);
    case KOS_RES_CONTROL_RETURNED: {
      KosOpResult r = kos_dynnode_on_control_returned(
          es, KosEvalState_CurrentNode(es));
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
        return EVAL_REPROLOGUE();
      return apply_kos_op_result(es, r);  /* one level: hook never emits CONTROL_RETURNED */
    }
    case KOS_RES_FATAL:
      return res;
    default:  /* NORMAL: unreachable in apply */
      return EVAL_FATAL();
  }
}
```

**`cg_handle_op_result` becomes three lines:**

```c
cg_handle_op_result: {
  KosOpResult r = apply_kos_op_result(es, op_res);
  if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
  return r;
}
```

`eval_dispatch_op_result` and `KOS_EVAL_DISPATCH_STEP` are deleted.

**Rule for `eval_event_*` helpers:** they return `KosOpResult` with no `goto`.
`REPROLOGUE` means "loop should continue." Terminal kinds mean "return from
`koskari_eval`." They never call `apply_kos_op_result` themselves — only
`apply` calls them.

### Category A — Drop dead `out_ev` parameters

These functions already mark `(void) out_ev` and write nothing to it. Pure
signature removal; no logic change.

| Function | File | New return / sig |
|----------|------|-----------------|
| `kos_dyn_install` | `dynchain.c:974` | `int (es, node)` |
| `kos_dyn_cc_raise_unwind` | `dyn_cc_raise.c:54` | `int (es, node, event)` |
| `kos_dyn_handler_uninstall_for_kind` and static uninstall helpers | `dynchain.c` | drop `out_ev` |

**`kos_dynnode_on_control_returned` is NOT in this category.** See Category D.

### Category B — Deliver functions take `PyObject *val` directly

These functions read `KosEvent_PAYLOAD(out_ev)` as their first act. Change
them to accept the value parameter directly; the kind is implicit in which
function is called.

| Function | File | New signature |
|----------|------|---------------|
| `kos_dyn_next_deliver_yield` | `dyn_next.c:143` | `int (es, PyObject *val)` |
| `kos_dyn_next_deliver_return` | `dyn_next.c:170` | `int (es, PyObject *val)` |
| `kos_dyn_foreach_deliver_yield_kos_agen` | `dyn_foreach.c:143` | `int (es, PyObject *val)` |
| `kos_dyn_foreach_deliver_return_kos_agen` | `dyn_foreach.c:165` | `int (es, PyObject *val)` |
| `kos_dyn_deliver_iterator_boundary_return_event` | `dynchain.c:2203` | `int (es, PyObject *val)` |
| `kos_dyn_deliver_iterator_boundary_yield_event` | `dynchain.c:2240` | `int (es, PyObject *val)` |
| `kos_try_deliver_iterator_boundary_after_pop` | `dynchain.c:2289` | `int (es, PyObject *val)` |

Return type stays `int` (0/-1); these functions do not produce AWAIT.

### Category C — Call family and PushOrAwait → `KosOpResult`

`KosInstallFunc` and `KosVectorInstallFunc` typedefs drop `out_ev` and return
`KosOpResult`. All implementations and call sites follow.

```c
/* Before */
typedef int (*KosInstallFunc)(KosEvalState *es, KosEvent *out_ev,
    PyObject *callable, PyObject *args, PyObject *kwds, int tail_call);

/* After */
typedef KosOpResult (*KosInstallFunc)(KosEvalState *es,
    PyObject *callable, PyObject *args, PyObject *kwds, int tail_call);
```

Old int → `KosOpResult` mapping for all call-family functions:

| Old int | `KosOpResult.kind` | `.val` |
|---------|--------------------|--------|
| `0` | `KOS_RES_NORMAL` | NULL |
| `1` (AWAIT) | `KOS_RES_AWAIT` | awaitable |
| `KOS_OP_RES_CONTROL_RETURNED` (2) | `KOS_RES_CONTROL_RETURNED` | NULL |
| `KOS_OP_RES_PARENT_RETURN_DELIVER` (4) | `KOS_RES_PARENT_RETURN` | val |
| `-1` | `KOS_RES_RAISE` | NULL |

Functions changing return type:

| Function | File |
|----------|------|
| `KosCallable_Call/CallArgs/CallNoArgs` | `callable.c` |
| `KosEvalState_Call/CallArgs/CallNoArgs` | `eval.h` / `eval_utils.c` |
| `KosEvalState_PushOrAwait` | `eval.h` / `eval_utils.c` |
| `kos_install_expression`, `kos_install_function`, `kos_vinstall_function` | `func.c` |
| `kos_install_boundmethod`, `kos_vinstall_boundmethod` | `func.c` |
| `kos_install_continuation` / `call_call_continuation` | `cont.c` |
| `kos_install_apply` | `utils.c` |

**Effect on opcodes:** every opcode `.c` file that has `KosEvent _op_ev = {0}`
eliminates it. The pattern:

```c
/* Before */
KosEvent _op_ev = {0};
int r = KosEvalState_Call(es, &_op_ev, callable, args, nargs, kwnames, tail);
return kos_op_result_from_int_step(r, &_op_ev);

/* After */
return KosEvalState_Call(es, callable, args, nargs, kwnames, tail);
```

`kos_op_result_from_int_step` and `kos_op_result_from_call_int` are deleted.
`KOS_OP_RES_CONTROL_RETURNED` and `KOS_OP_RES_PARENT_RETURN_DELIVER` defines
are deleted (the `KosOpResult` kinds replace them).

### Category D — Dynchain wind/unwind/pop and `on_control_returned` → `KosOpResult`

Functions that can produce AWAIT (for async `WITH __enter__`/`__exit__`).

`kos_dynnode_on_control_returned` is **upgraded** here (not dead-param removal):
its WIND/WITH resume paths may need to signal AWAIT or RAISE, and returning
`KosOpResult` gives them that channel. Whether the current `out_ev` checks in the
hook are live or dead is a separate question; the upgrade makes future paths
safe and removes the need for `apply` to check `out_ev` after the call.

```c
/* Before */
int kos_dynnode_on_control_returned(KosEvalState *es, KosDynNode *node, KosEvent *out_ev);

/* After */
KosOpResult kos_dynnode_on_control_returned(KosEvalState *es, KosDynNode *node);
```

All functions returning `KosOpResult`:

| Function | File |
|----------|------|
| `kos_dynnode_on_control_returned` | `dynchain.c:289` |
| `kos_dyn_dyn_with_wind` | `dyn_with.c:418` |
| `kos_dyn_with_unwind` | `dyn_with.c:460` |
| `kos_dyn_wind` | `dynchain.c:1079` |
| `kos_dyn_unwind` | `dynchain.c:1110` |
| `kos_dyn_pop_top` | `dyn_walk.c:162` |
| `kos_dyn_pop_nonbarrier_top` | `dyn_walk.c:179` |
| `kos_dyn_uninstall_core` | `dynchain.c:899` |
| `kos_dyn_uninstall_handler` | `dynchain.c:876` |
| `kos_dyn_exception_unwind` | `dyn_walk.c` |

`try_local_exception_handler` return type:

| Old int | `KosOpResult.kind` |
|---------|--------------------|
| `1` (local handler found) | `KOS_RES_REPROLOGUE` |
| `0` (boundary raise) | `KOS_RES_RAISE` (PyErr set, context dead) |
| AWAIT from async WITH __exit__ | `KOS_RES_AWAIT` |
| `-1` (fatal) | `KOS_RES_FATAL` |

**`KOS_DYN_UNWIND_ASYNC_STARTED` substitution rule:** Use `EVAL_AWAIT(awaitable)`
when the awaitable flows directly from the async call result. Use
`EVAL_REPROLOGUE()` when the WITH node handles resumption via its phase state
machine (pending phase set on node, eval re-enters and processes via the
CONTROL_RETURNED path). Verify with the matrix in the verification section.

### Category E — `eval_event_*` → `KosOpResult`, no `out_ev`

All event helpers drop `out_ev` and return `KosOpResult`. They never
call `apply_kos_op_result` — only `apply` calls them.

| Function | Signature | Returns |
|----------|-----------|---------|
| `eval_event_raise(es)` | `(KosEvalState *)` | REPROLOGUE / AWAIT / RAISE / FATAL |
| `eval_event_yield(es, val)` | `(KosEvalState *, PyObject *)` | REPROLOGUE / YIELD / RAISE |
| `eval_event_await(es, val)` | `(KosEvalState *, PyObject *)` | REPROLOGUE / AWAIT / RAISE |
| `eval_event_return(es, val)` | `(KosEvalState *, PyObject *)` | REPROLOGUE / RETURN / AWAIT / RAISE |
| `eval_parent_return_deliver(es, val)` | `(KosEvalState *, PyObject *)` | REPROLOGUE / AWAIT / RAISE |
| `eval_event_return_deliver_to_boundary(es, val)` | `(KosEvalState *, PyObject *)` | REPROLOGUE / AWAIT / RAISE |

### Thin `RETURN.c`

`RETURN.c` currently owns Phase 2a–2c (unwind, frame pop, parent delivery,
boundary re-emit). This duplicates policy in `eval_event_return`. Move 2b/2c
into `eval_event_return` so `RETURN.c` contains only:

- Pop the return value from the operand stack
- Delimited path: if prompts active, arm plan and `EVAL_REPROLOGUE()`
- `EVAL_RETURN(val)` — that's it

`eval_event_return` owns: `KOS_TRANS_WALK_RETURN_TO_FRAME`, finally-taking-return
staging, async unwind, `kos_dyn_pop_frame`, parent delivery, iterator boundary
routing. Same policy in one place.

If this proves entangled with transfer-plan timing, extract it to a follow-up
commit in the same branch rather than blocking Steps 6–9.

### Category F — `koskari_eval` → `KosOpResult`

```c
/* Before */
int koskari_eval(KosContext *ctx, KosEvent *out_ev,
                 PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);

/* After */
KosOpResult koskari_eval(KosContext *ctx,
                         PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);
```

`koskari_eval` holds no local `KosEvent`. It calls `apply_kos_op_result` for
non-`NORMAL` opcode results and returns the terminal `KosOpResult` from there.
`eval_dispatch_op_result` is deleted.

`handle_continuation_apply` in `eval.c` drops `out_ev`, returns `KosEvalMode`
(unchanged — it signals which mode to enter next, no value).

`xfer_emit_cc_unwind_signal` in `eval_chain.c` calls
`kos_eval_prepare_boundary_raise` then returns `EVAL_RAISE()`.

### Category G — Callers of `koskari_eval`

Four files replace `KosEvent ev = {0}` + `int res = koskari_eval(ctx, &ev, ...)`
with `KosOpResult res = koskari_eval(ctx, ...)`.

```c
/* Before */
KosEvent ev = {0};
int r = koskari_eval(ctx, &ev, send_val, NULL, NULL);
if (r < 0) return NULL;  /* fatal */
switch (KosEvent_KIND(&ev)) {
  case KOS_EV_RETURN: val = KosEvent_PAYLOAD(&ev); ...
  case KOS_EV_RAISE:
    if (KosEvent_EXC_TYPE(&ev)) {
      PyErr_SetObject(KosEvent_EXC_TYPE(&ev), KosEvent_PAYLOAD(&ev));
      ...
    }
    return NULL;
  ...
}

/* After */
KosOpResult res = koskari_eval(ctx, send_val, NULL, NULL);
switch (res.kind) {
  case KOS_RES_RETURN: val = res.val; ...
  case KOS_RES_RAISE:  return NULL;   /* PyErr already set */
  case KOS_RES_YIELD:  val = res.val; ...
  case KOS_RES_AWAIT:  val = res.val; ...
  case KOS_RES_FATAL:  return NULL;
}
```

The `exc_type` restoration block is entirely eliminated in all four callers.

`YIELD_FROM.c`'s internal drive loop and `yield_from_emit_yield` drop their
`KosEvent *out_ev` parameters; the drive loop switches on `KosOpResult.kind`
from `koskari_eval`.

---

## Implementation sequence

Run `tox -qe test` after every step. Do not proceed past a red step.

| Step | Work | Notes |
|------|------|-------|
| **S1** | Add `kos_eval_prepare_boundary_raise`; retire `kos_eval_emit_raise_from_pyerr` and its four call sites | RAISE invariant established |
| **S2** | Category B: deliver functions take `PyObject *val` | Mechanical; callers pass value directly |
| **S3** | Category D: dynchain wind/unwind/pop/exception_unwind → `KosOpResult`; `try_local_exception_handler` → `KosOpResult`; verify async-unwind matrix | Behavior-sensitive; see verification §3 |
| **S3b** | `kos_dynnode_on_control_returned` → `KosOpResult`; Category A dead-param drops on dynchain helpers | With S3 since files overlap |
| **S4** | Category C: `KosInstallFunc` typedef + all install implementations → `KosOpResult`; `KosCallable_*` and `KosEvalState_Call*/PushOrAwait` → `KosOpResult`; delete bridges; delete `KOS_OP_RES_*` defines; clean all opcode `.c` files | Largest mechanical step; all `KosEvent _op_ev` locals disappear |
| **S5** | Category E + `apply_kos_op_result`: `eval_event_*` drop `out_ev`, return `KosOpResult`; introduce `apply_kos_op_result`; collapse `cg_handle_op_result` to three lines | |
| **S5b** | Thin `RETURN.c`: move Phase 2b/2c to `eval_event_return` | May be deferred to its own commit if entangled |
| **S6** | Category F: `koskari_eval` → `KosOpResult`, no `out_ev`; delete `eval_dispatch_op_result` and `KOS_EVAL_DISPATCH_STEP` | |
| **S7** | Category G: update `coro.c`, `iter.c`, `demand.c`, `YIELD_FROM.c` | |
| **S8** | Delete `KosEvent` struct/enum/macros/forwards; delete `KosEvalResult` if present; run grep verification | Final |

---

## Verification

### §1 — Elimination grep

```bash
grep -rn 'KosEvent\|KOS_EV_\b' koskari/lib/ --include='*.c' --include='*.h'
# nothing
```

Also check tests and Python layer if they reference `KosEvent` by name.

### §2 — RETURN.c criterion

```bash
grep -n 'KOS_TRANS_WALK_RETURN_TO_FRAME' koskari/lib/opcode/RETURN.c
# empty or inside delimited-only helper
grep -n 'kos_dyn_pop_frame\|parent.*deliver\|deliver.*parent' koskari/lib/opcode/RETURN.c
# empty
```

### §3 — Async-unwind behavioral matrix (before merging S3)

| Scenario | Expected | How to check |
|----------|----------|--------------|
| WIND after-thunk during RETURN unwind | Thunk runs; no spurious host AWAIT | `test_dyn_wind.py` |
| WITH async `__exit__` during exception unwind | Exception unwind suspends, resumes, re-raises or swallows correctly | `test_with_async.py` or equiv |
| WITH async `__enter__` during install | Suspends on AWAIT; resumes to REPROLOGUE | `test_with_async.py` |
| Transfer plan step starts async wind | Plan stays armed; same `koskari_eval` call | `test_reset_shift_nested.py` |
| Demand context + async WITH | Demand denial fires; no AWAIT escapes | `test_demand_sync.py` |
| Nested `koskari_eval` from `YIELD_FROM` | Sub-driver switches on `KosOpResult.kind` correctly | `test_yield_from.py` |

If a row fails when `UNWIND_ASYNC_STARTED` becomes `KOS_RES_AWAIT`, use
`KOS_RES_REPROLOGUE` plus node phase for that path only.

### §4 — Full test suite

```bash
tox -qe test
# 3212 passed, 10 skipped (or better)
```

---

## What the eval story looks like after this work

```
koskari_eval:
  entry → eval_phase_entry (KosEvalMode dispatch)
  loop:
    drain transfer plan
    pending PyErr → apply_kos_op_result(EVAL_RAISE())
    native frame? → run handler → apply_kos_op_result(result)
    GIL poll
    fetch insn → op_xxx(es, insn)        ← opcode returns KosOpResult
    NORMAL → tail dispatch (hot path)
    else   → apply_kos_op_result(result) → if REPROLOGUE goto loop; else return result

apply_kos_op_result:
  REPROLOGUE → return (caller gotos loop)
  RAISE      → eval_event_raise(es)
  YIELD      → eval_event_yield(es, val)
  AWAIT      → eval_event_await(es, val)
  RETURN     → eval_event_return(es, val)
  PARENT_RETURN → eval_parent_return_deliver(es, val)
  CONTROL_RETURNED → kos_dynnode_on_control_returned → apply once
  FATAL      → return FATAL

eval_event_*(es, val?):
  handle the event; return KosOpResult
  REPROLOGUE if loop continues; terminal kind if koskari_eval should stop

koskari_eval returns KosOpResult to caller (coro.c / iter.c / demand.c / YIELD_FROM.c)
  RETURN / YIELD / AWAIT → res.val is the value
  RAISE / FATAL          → PyErr is set
```

---

## Track C — Optional post-landing cleanup

Not required for grep criterion. After S8 stable:

- **C1:** Collapse `KosEvalStep` / `KosEvalMode` into `KosOpResult` where phase
  functions naturally return values; delete dead enum variants.
- **C2:** `kos_dyn_route_boundary` — cap-driven router for iterator boundary
  delivery, replacing scattered `kos_dyn_need_kos_agen_boundary_deliver` checks
  in `eval_event_yield` / `eval_parent_return_deliver` / `eval_event_return`.
- **C3:** `KosPhase` explicit two-state loop if the prologue remains hard to
  reason about after S5.

On completion, move `kosevent-elimination.md` and
`kosevent-elimination-simplification-response.md` to `design/archive/`.

# The end.
