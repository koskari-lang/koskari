# KosEvent Elimination

**Goal:** Delete `KosEvent` / `KosEventKind` / `KOS_EV_*` entirely. No replacement
struct. No pointer threading. Every function that can produce a boundary signal
returns that signal as its value. Callers relay or act; they do not forward a
pointer for someone else to fill.

**Completion criterion:**

```bash
grep -rn 'KosEvent\|KOS_EV_\b' koskari/lib/ --include='*.c' --include='*.h'
```

returns nothing (outside of comments being removed).

---

## What gets deleted

- `KosEvent` struct and `KosEventKind` enum (in `eval.h`)
- `KOS_EV_NONE`, `KOS_EV_RETURN`, `KOS_EV_RAISE`, `KOS_EV_YIELD`, `KOS_EV_AWAIT`
- `KosEvent_KIND`, `KosEvent_PAYLOAD`, `KosEvent_EXC_TYPE` accessor macros
- `KosEvent.exc_type` field and the `PyErr_Fetch`/restore pattern in callers
- `kos_eval_emit_raise_from_pyerr` (replaced; see RAISE invariant below)
- Bridge functions: `kos_op_result_from_int_step`, `kos_op_result_from_call_int`
- `KOS_DYN_UNWIND_ASYNC_STARTED` as a special return code: absorbed into
  `KosOpResult` AWAIT (where applicable) or plain `int` (where the awaitable
  was always already on the stack/frame)

---

## RAISE invariant

**For every function that returns `KOS_RES_RAISE` (or signals an error that maps
to RAISE): `PyErr` is set on the calling thread. The caller never needs to
restore it from a struct field.**

This eliminates `KosEvent.exc_type`, the `PyErr_Fetch`/`KosEvent_EXC_TYPE`
pattern throughout `coro.c`, `iter.c`, `demand.c`, and the corresponding
stores in `kos_eval_emit_raise_from_pyerr`.

**`kos_eval_emit_raise_from_pyerr` replacement.** The function currently:
1. Flushes live registers (needed for traceback)
2. `PyErr_Fetch` → injects traceback → stores type+val in `out_ev` → clears PyErr
3. Marks context dead

After the change, a replacement `kos_eval_prepare_boundary_raise(ctx, es)`:
1. Flushes live registers
2. `PyErr_Fetch` → `inject_eval_frame_traceback` → `PyErr_Restore` (inject in-place,
   leave PyErr set)
3. Marks context dead
4. Returns nothing; callers return `EVAL_RAISE()` / `KOS_RES_RAISE`

Call sites in `dyn_walk.c` and `eval_chain.c` call
`kos_eval_prepare_boundary_raise` then return `KOS_RES_RAISE` (or the
equivalent for their return type).

---

## Change categories

### A — Drop dead `out_ev` parameters (no logic change)

These functions already do `(void) out_ev` at every call site; removing
the parameter is mechanical.

| Function | File | New signature |
|----------|------|---------------|
| `kos_dynnode_on_control_returned` | `dynchain.c:289` | `int (es, node)` |
| `kos_dyn_install` | `dynchain.c:974` | `int (es, node)` |
| `kos_dyn_cc_raise_unwind` | `dyn_cc_raise.c:54` | `int (es, node, event)` |
| `kos_dyn_handler_uninstall_for_kind` and related static helpers | `dynchain.c:876–1168` | drop `out_ev` |
| `kos_install_expression`, `kos_install_function`, `kos_vinstall_function` | `func.c` | `KosOpResult (es, callable, args, kwds, tail_call)` |
| `kos_install_continuation` / `call_call_continuation` | `cont.c` | `KosOpResult (es, callable, args, tail_call)` |

Note: the install functions in `func.c` change their return type to `KosOpResult`
(matching the `KosInstallFunc` typedef change in category C) but their logic is
unchanged — they return `EVAL_NORMAL()` (was 0) or `EVAL_RAISE()` (was -1).

### B — Deliver functions: take `PyObject *val` directly

These functions currently extract `KosEvent_PAYLOAD(out_ev)` as their first
act. Change them to take the value parameter directly.

| Function | File | New signature |
|----------|------|---------------|
| `kos_dyn_next_deliver_yield` | `dyn_next.c:143` | `int (es, PyObject *val)` |
| `kos_dyn_next_deliver_return` | `dyn_next.c:170` | `int (es, PyObject *val)` |
| `kos_dyn_foreach_deliver_yield_kos_agen` | `dyn_foreach.c:143` | `int (es, PyObject *val)` |
| `kos_dyn_foreach_deliver_return_kos_agen` | `dyn_foreach.c:165` | `int (es, PyObject *val)` |
| `kos_dyn_deliver_iterator_boundary_return_event` | `dynchain.c:2203` | `int (es, PyObject *val)` |
| `kos_dyn_deliver_iterator_boundary_yield_event` | `dynchain.c:2240` | `int (es, PyObject *val)` |
| `kos_try_deliver_iterator_boundary_after_pop` | `dynchain.c:2289` | `int (es, PyObject *val)` |

Callers that currently build `out_ev` to pass a value to these functions pass
the `PyObject *` directly instead.

`kos_try_deliver_iterator_boundary_after_pop` currently sets
`KosEvent_KIND/PAYLOAD(out_ev)` before calling — that goes away; the value is
passed as `val` and the kind is implicit in the function being called.

### C — Call family and PushOrAwait: return `KosOpResult`

These functions currently return `int` (0 = continue, 1 = AWAIT,
`KOS_OP_RES_*` = other, -1 = error) and write the awaitable into `out_ev`.
They change to return `KosOpResult` with the awaitable in `.val` for AWAIT.

`KosInstallFunc` and `KosVectorInstallFunc` typedefs in `callable.h` change to
return `KosOpResult` and drop `out_ev`.

| Function | File | New return |
|----------|------|------------|
| `KosInstallFunc` typedef | `callable.h:42` | `KosOpResult (es, callable, args, kwds, tail_call)` |
| `KosVectorInstallFunc` typedef | `callable.h:51` | `KosOpResult (es, callable, args, nargs, kwnames, tail_call)` |
| `KosCallable_Call` | `callable.c:56` | `KosOpResult` |
| `KosCallable_CallArgs` | `callable.c:88` | `KosOpResult` |
| `KosCallable_CallNoArgs` | `callable.c:99` | `KosOpResult` |
| `KosEvalState_Call` | `eval.h:836` | `KosOpResult` |
| `KosEvalState_CallArgs` | `eval.h:854` | `KosOpResult` |
| `KosEvalState_CallNoArgs` | `eval.h:869` | `KosOpResult` |
| `KosEvalState_PushOrAwait` | `eval.h:894` | `KosOpResult` |

Old int protocol → `KosOpResult` mapping:

| Old int | `KosOpResult.kind` | `.val` |
|---------|--------------------|--------|
| 0 | `KOS_RES_NORMAL` | NULL |
| 1 (AWAIT) | `KOS_RES_AWAIT` | awaitable (was `out_ev.payload`) |
| `KOS_OP_RES_CONTROL_RETURNED` (2) | `KOS_RES_CONTROL_RETURNED` | NULL |
| `KOS_OP_RES_PARENT_RETURN_DELIVER` (4) | `KOS_RES_PARENT_RETURN` | val |
| -1 | `KOS_RES_RAISE` | NULL (PyErr set) |

**Effect on opcodes:** All opcode `.c` files that currently do:

```c
KosEvent _op_ev = {0};
int r = KosEvalState_Call(es, &_op_ev, callable, args, nargs, kwnames, tail);
return kos_op_result_from_int_step(r, &_op_ev);
```

become:

```c
return KosEvalState_Call(es, callable, args, nargs, kwnames, tail);
```

No local `KosEvent`. No bridge function. The bridge functions
(`kos_op_result_from_int_step`, `kos_op_result_from_call_int`) are deleted.

`kos_install_apply` in `utils.c` delegates to `inner(es, callable, ...)` and
returns the result directly — the delegation chain is now fully typed.

### D — Dynchain wind/unwind/pop: return `KosOpResult`

These can produce AWAIT (for async `WITH __enter__` and `__exit__`). They
return `KosOpResult` so the awaitable flows up naturally.

| Function | File | New return |
|----------|------|------------|
| `kos_dyn_dyn_with_wind` | `dyn_with.c:418` | `KosOpResult` |
| `kos_dyn_with_unwind` | `dyn_with.c:460` | `KosOpResult` |
| `kos_dyn_wind` | `dynchain.c:1079` | `KosOpResult` |
| `kos_dyn_unwind` | `dynchain.c:1110` | `KosOpResult` |
| `kos_dyn_pop_top` | `dyn_walk.c:162` | `KosOpResult` |
| `kos_dyn_pop_nonbarrier_top` | `dyn_walk.c:179` | `KosOpResult` |
| `kos_dyn_uninstall_core` | `dynchain.c:899` | `KosOpResult` |
| `kos_dyn_uninstall_handler` | `dynchain.c:876` | `KosOpResult` |
| `kos_dyn_exception_unwind` | (dyn_walk.c) | `KosOpResult` |

Where `KOS_DYN_UNWIND_ASYNC_STARTED` was returned: return
`KOS_RES_AWAIT` with the awaitable in `.val`. Callers check `res.kind ==
KOS_RES_AWAIT` rather than `res == KOS_DYN_UNWIND_ASYNC_STARTED`.

Where the function had no async path and only signaled int 0/-1: return
`KosOpResult` with NORMAL or RAISE, or keep returning `int` if the caller
doesn't need the full bandwidth — prefer whichever keeps the calling code
more direct. Do not force `KosOpResult` on functions that genuinely only
signal success/fail with no value to carry.

`try_local_exception_handler` in `dyn_walk.c` currently returns int (1 =
local handler, 0 = boundary raise, -1 = fatal). It becomes:
- `KOS_RES_REPROLOGUE` — local handler found, eval continues
- `KOS_RES_RAISE` — boundary raise, PyErr set, context dead
- `KOS_RES_AWAIT` — async WITH __exit__ during unwind started
- `KOS_RES_FATAL` — internal fault

### E — `eval_event_*` helpers: return `KosOpResult`, drop `out_ev`

These are the main eval event dispatch helpers. They currently take
`KosEvent *out_ev` and return `KosEvalStep`. They change to return
`KosOpResult` and take no `out_ev`.

| Function | New return | Notes |
|----------|------------|-------|
| `eval_event_raise(es)` | `KosOpResult` | RAISE or REPROLOGUE (TRY caught) or AWAIT (async WITH __exit__) |
| `eval_event_yield(es, val)` | `KosOpResult` | YIELD (stop) or REPROLOGUE (boundary delivered in-loop) or RAISE |
| `eval_event_await(es, val)` | `KosOpResult` | AWAIT (stop) or RAISE (demand denial) |
| `eval_event_return(es, val)` | `KosOpResult` | RETURN (top-level) or REPROLOGUE (parent deliver) or RAISE |
| `eval_parent_return_deliver(es, val)` | `KosOpResult` | REPROLOGUE or AWAIT or RAISE |
| `eval_event_return_deliver_to_boundary(es, val)` | `KosOpResult` | REPROLOGUE or AWAIT or RAISE |
| `kos_eval_post_opcode_control_returned_after_hook(es)` | `KosOpResult` | REPROLOGUE or AWAIT or YIELD or RETURN or RAISE |

`koskari_eval` returns the `KosOpResult` from these helpers directly when
they signal a terminal kind (YIELD, AWAIT, RETURN, RAISE, FATAL). It
continues (goto prologue) for REPROLOGUE.

**`cg_handle_op_result` dispatch becomes:**

```c
KosOpResult res = /* from opcode */;
switch (res.kind) {
  case KOS_RES_REPROLOGUE: goto cg_prologue;
  case KOS_RES_RAISE:      { KosOpResult r = eval_event_raise(es);
                              if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                              return r; }
  case KOS_RES_YIELD:      { KosOpResult r = eval_event_yield(es, res.val);
                              if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                              return r; }
  case KOS_RES_AWAIT:      { KosOpResult r = eval_event_await(es, res.val);
                              if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                              return r; }
  case KOS_RES_RETURN:     { KosOpResult r = eval_event_return(es, res.val);
                              if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                              return r; }
  case KOS_RES_PARENT_RETURN: { KosOpResult r = eval_parent_return_deliver(es, res.val);
                                  if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                                  return r; }
  case KOS_RES_CONTROL_RETURNED:
                            { KosOpResult r = kos_eval_post_opcode_control_returned_after_hook(es);
                              if (r.kind == KOS_RES_REPROLOGUE) goto cg_prologue;
                              return r; }
  case KOS_RES_FATAL:      return EVAL_FATAL();
  default: /* NORMAL handled in hot path */ break;
}
```

The `KOS_EVAL_DISPATCH_STEP` macro is retired; logic is inline.

### F — `koskari_eval`: return `KosOpResult`, no `out_ev` parameter

```c
/* Before */
int koskari_eval(KosContext *ctx, KosEvent *out_ev,
                 PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);

/* After */
KosOpResult koskari_eval(KosContext *ctx,
                         PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);
```

`koskari_eval` no longer holds a local `KosEvent`. When an event helper
returns a terminal `KosOpResult`, that result is returned directly from
`koskari_eval`. The result carries the value in `.val`.

Phase-level step functions (`eval_phase_entry`, `eval_step_drain_transfer_plan`,
`eval_step_native_iteration`) continue returning `KosEvalStep` since they do
not carry values; `koskari_eval` maps fatal → `EVAL_FATAL()`, stop →
propagate the result already in flight, reprologue → continue.

`eval_dispatch_op_result` (used from non-`goto` contexts) similarly becomes
unnecessary; callers use `KosOpResult` directly.

`handle_continuation_apply` in `eval.c` currently takes `KosEvent *out_ev`;
it drops the parameter and returns `KosEvalMode` (unchanged — it only signals
which mode to enter next, no value to carry).

`xfer_emit_cc_unwind_signal` in `eval_chain.c` calls
`kos_eval_emit_raise_from_pyerr`; after the change it calls
`kos_eval_prepare_boundary_raise` and returns `EVAL_RAISE()`.

### G — Callers of `koskari_eval`

Four files create a local `KosEvent ev = {0}` and pass `&ev` to
`koskari_eval`. After the change, they receive a `KosOpResult` and switch on
`res.kind`.

| File | Callers |
|------|---------|
| `coro.c` | `KosCoroutine_Send`, `KosCoroutine_Throw` (~lines 236, 371) |
| `iter.c` | async-gen step callers (~lines 78, 422) |
| `demand.c` | demand-sync driver (~lines 204, 955) |
| `opcode/YIELD_FROM.c` | nested eval driver (~line 637) |

**Pattern change:**

```c
/* Before */
KosEvent ev = {0};
int res = koskari_eval(ctx, &ev, send_val, NULL, NULL);
if (res < 0) { /* fatal */ return NULL; }
switch (KosEvent_KIND(&ev)) {
  case KOS_EV_RETURN:
    val = KosEvent_PAYLOAD(&ev);
    ...
  case KOS_EV_RAISE:
    if (KosEvent_EXC_TYPE(&ev)) {
      PyErr_SetObject(KosEvent_EXC_TYPE(&ev), KosEvent_PAYLOAD(&ev));
      Py_DECREF(KosEvent_EXC_TYPE(&ev));
      Py_XDECREF(KosEvent_PAYLOAD(&ev));
    } else if (KosEvent_PAYLOAD(&ev)) { ... }
    return NULL;
  ...
}

/* After */
KosOpResult res = koskari_eval(ctx, send_val, NULL, NULL);
switch (res.kind) {
  case KOS_RES_RETURN:
    val = res.val;  /* take ownership */
    ...
  case KOS_RES_RAISE:
    return NULL;    /* PyErr already set */
  case KOS_RES_YIELD:
    val = res.val;
    ...
  case KOS_RES_AWAIT:
    val = res.val;
    ...
  case KOS_RES_FATAL:
    return NULL;    /* PyErr set by fatal path */
}
```

`YIELD_FROM.c`'s internal `yield_from_emit_yield(es, out_ev, tos)` function
(which currently fills `out_ev` with `KOS_EV_YIELD`) becomes `EVAL_YIELD(*tos)`
returned directly from the opcode path that calls it, or, if it is a real
function, it returns `KosOpResult` and the caller returns that. The internal
drive loop in `YIELD_FROM.c` that calls `koskari_eval` on a sub-generator now
switches on `KosOpResult.kind` and passes `res.val` directly.

---

## Implementation sequence

Work **bottom-up**: each step makes the layer below clean so the layer above
has nothing to bridge.

### Step 1 — `kos_eval_prepare_boundary_raise`; retire `kos_eval_emit_raise_from_pyerr`

Add `kos_eval_prepare_boundary_raise(KosContext *ctx, KosEvalState *es)` in
`eval.c` (static, not exported) that flushes live regs, fetch-inject-restore
PyErr for traceback, and marks context dead. Update the four call sites in
`dyn_walk.c` and `eval_chain.c` to call the replacement and return
`KOS_RES_RAISE`. Delete `kos_eval_emit_raise_from_pyerr` declaration from
`eval.h` and definition from `eval.c`.

### Step 2 — Category B: deliver functions take `PyObject *val`

Change `kos_dyn_next_deliver_yield/return`, `kos_dyn_foreach_deliver_*`,
`kos_dyn_deliver_iterator_boundary_*`, `kos_try_deliver_iterator_boundary_after_pop`.
Update all call sites in `dynchain.c`, `eval.c`, `dyn_next.c` to pass the value
directly. Build confirms no callers pass `out_ev`.

### Step 3 — Category D: dynchain wind/unwind/pop return `KosOpResult`

Change `kos_dyn_with_unwind`, `kos_dyn_dyn_with_wind`, `kos_dyn_unwind`,
`kos_dyn_wind`, `kos_dyn_pop_top`, `kos_dyn_pop_nonbarrier_top`,
`kos_dyn_uninstall_core`, `kos_dyn_uninstall_handler`, `kos_dyn_exception_unwind`.
Replace `KOS_DYN_UNWIND_ASYNC_STARTED` returns with `EVAL_AWAIT(awaitable)`.
Update `try_local_exception_handler` return type to `KosOpResult`.

### Step 4 — Category A: drop dead `out_ev` parameters from dynchain

`kos_dynnode_on_control_returned`, `kos_dyn_install`, `kos_dyn_cc_raise_unwind`,
and the static helpers in `dynchain.c` that already mark `(void) out_ev`. Pure
signature removal.

### Step 5 — Category C: `KosInstallFunc` typedef and implementations

Change `KosInstallFunc` / `KosVectorInstallFunc` typedefs to return
`KosOpResult` and drop `out_ev`. Update all implementations:
`kos_install_expression`, `kos_install_function`, `kos_vinstall_function`,
`kos_install_boundmethod`, `kos_vinstall_boundmethod` (`func.c`),
`kos_install_continuation` (`cont.c`), `kos_install_apply` (`utils.c`).

Change `KosCallable_Call/CallArgs/CallNoArgs` and
`KosEvalState_Call/CallArgs/CallNoArgs` and `KosEvalState_PushOrAwait` to
return `KosOpResult`. Delete bridge functions `kos_op_result_from_int_step`
and `kos_op_result_from_call_int`.

At this point, **all opcode `.c` files** that have `KosEvent _op_ev = {0}`
can be cleaned up: remove the local, replace the bridge call with a direct
return of the `KosOpResult` from `KosEvalState_Call*` or `KosEvalState_PushOrAwait`.

### Step 6 — Category E: `eval_event_*` drop `out_ev`, return `KosOpResult`

Rewrite `eval_event_raise`, `eval_event_yield`, `eval_event_await`,
`eval_event_return`, `eval_parent_return_deliver`,
`eval_event_return_deliver_to_boundary`, and
`kos_eval_post_opcode_control_returned_after_hook` to return `KosOpResult`
with no `out_ev` parameter. Update `cg_handle_op_result` dispatch as shown
in category E above.

### Step 7 — Category F: `koskari_eval` new signature

Change `koskari_eval` to return `KosOpResult` and drop `out_ev`. Remove
`eval_dispatch_op_result`. Update `eval_phase_entry` and related phase
functions as needed.

### Step 8 — Category G: callers of `koskari_eval`

Update `coro.c`, `iter.c`, `demand.c`, `YIELD_FROM.c`. Remove all local
`KosEvent ev = {0}` declarations. Remove `exc_type` restoration code. Switch
on `res.kind`, use `res.val` for the value.

### Step 9 — Delete `KosEvent`

Remove from `eval.h`: `KosEventKind` enum, `KOS_EV_*` constants,
`KosEvent` struct, `KosEvent_KIND/PAYLOAD/EXC_TYPE` macros, `lib.h` forward
declaration of `struct KosEvent`, `callable.h` forward declaration.

Run the grep verification. Any remaining hit is a bug to fix before
committing this step.

---

## What stays

- `KosOpResult {KosOpResultKind kind; PyObject *val}` and all `KOS_RES_*` / `EVAL_*`
- `KosEvalStep` and `KOS_STEP_*` for phase-level step functions within `eval.c`
  that signal only REPROLOGUE / STOP / FATAL (no value to carry)
- `KOS_OP_RES_CONTROL_RETURNED` and `KOS_OP_RES_PARENT_RETURN_DELIVER` integer
  defines (used in `KosEvalState_PushOrAwait` int-return protocol; absorbed into
  `KosOpResult` mapping in step 5, then the defines themselves can be removed)
- `KosSuspensionKind` and `KosEvalState_SetSuspensionEvent` (these are context-
  state markers separate from event signaling; they remain)
- `KosXferEvent` (transfer-plan event kind, separate concept; unchanged)

# The end.
