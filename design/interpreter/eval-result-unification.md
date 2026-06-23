# Eval result unification: collapsing KosOpOutcome + out_ev into KosOpResult

This document is a follow-up to
[event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md).
That document addressed centralising event handling inside the eval loop.
This document addresses the layer below it: the communication channel from
opcode handlers back to the eval loop.

The goal is to replace the current **two-channel** protocol (a `KosOpOutcome`
return value *and* a mutated `out_ev` side channel) with a single
**`KosOpResult`** struct, and to build the eval dispatch switch around that
struct so behavior is derivable from one table lookup rather than three
cascading dispatch phases.

**This is a speculative design document.** §10 contains an honest assessment
of what is a genuine structural win versus what amounts to renaming. Read that
section before committing to the work.

Companion documents:
[event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md),
[evaluator.md](evaluator.md).

---

## 1. The problem: two output channels from one opcode

Every opcode handler today has the signature:

```c
KosOpOutcome op_NAME(KosEvalState *es, KosEvent *out_ev, KosInsn *insn);
```

It has two output channels:

**Channel A — `KosOpOutcome` return value:**

```c
typedef struct KosOpOutcome {
  KosLop    mode;      // RUN | PROLOGUE | POST | FAIL
  uint8_t   post_tag;  // TAG_SUSPEND | TAG_CONTROL_RETURNED | TAG_ERR | TAG_PARENT_RETURN
  PyObject *val;       // unused today
} KosOpOutcome;
```

**Channel B — `out_ev` mutation:**

```c
// typical in YIELD.c
KosEvent_KIND(out_ev) = KOS_EV_YIELD;
KosEvent_PAYLOAD(out_ev) = val;
return KOS_OP_POST_SUSPEND();  // channel A says "go check out_ev"
```

The two channels are coupled by convention: if an opcode sets `out_ev`, it
must also return `KOS_LOP_POST`; if it returns `KOS_LOP_RUN`, `out_ev` must
be untouched. This coupling is invisible to the type system.

The eval loop then translates:

```
op_out.mode == KOS_LOP_POST
  → kos_op_post_tag_to_res(op_out.post_tag)         [dispatch 1]
      → eval_phase_post_opcode(es, out_ev, op_res)  [dispatch 2]
          → check KosEvent_KIND(out_ev)              [dispatch 3]
              → eval_event_yield / raise / ...       [dispatch 4]
```

Four dispatch levels to act on one semantic event.

---

## 2. Current encoding layers

| Layer | Type | Values | Purpose |
|---|---|---|---|
| Opcode outcome | `KosLop` | RUN, PROLOGUE, POST, FAIL | "what should the loop do next" |
| Post-opcode tag | `uint8_t` | SUSPEND, CONTROL_RETURNED, ERR, PARENT_RETURN | "which post-opcode path" |
| Post-opcode res | `int` (KOS_OP_RES_*) | -1, 0, 2, 4 | same thing, re-encoded |
| Event kind | `KosEventKind` | NONE, RETURN, RAISE, YIELD, AWAIT, POST_OPCODE | "what happened semantically" |
| Eval step | `KosEvalStep` | CONTINUE, REPROLOGUE, STOP_EVENT, FATAL_ERROR | "what the loop does after helpers" |
| Eval mode | `KosEvalMode` | NORMAL, TRANSFER, EXCEPTION, RAISE, AWAIT, YIELD, YIELD_FROM, RETURN | "how to re-enter after suspension" |
| Suspension event | `KosSuspensionEvent` | NONE, AWAIT, YIELD | "why we suspended" (stored on ctx) |
| Driver result | `KosEvalResultKind` | RETURN, RAISE, YIELD, AWAIT | "what koskari_eval tells its caller" |

Eight distinct encoding spaces, several with overlapping value sets, none
sharing a type with the others.

---

## 3. Proposed `KosOpResult` struct

The proposed replacement for `KosOpOutcome` (and the `out_ev` side channel
from opcodes) is a single typed struct:

```c
/**
  KosOpResultKind encodes the complete outcome of one opcode execution.

  NORMAL    — opcode ran cleanly; advance PC and dispatch next opcode.
              val is NULL (opcode already pushed to stack if needed).
  REPROLOGUE — restart the dispatch prologue before the next opcode.
              Covers installed calls, transfer plan changes, and any
              other case where the prologue's checks must re-run.
  YIELD     — generator is yielding val (new ref). Eval owns val.
  AWAIT     — suspending on Python awaitable val (new ref). Eval owns val.
  RETURN    — frame is returning val (new ref). Eval owns val.
  RAISE     — exception is set in PyErr. val is NULL.
  CONTROL_RETURNED — a WIND/WITH control thunk completed; run the hook.
              val is NULL. Emitted by native handlers, not opcode bytecode.
  FATAL     — unrecoverable internal error; koskari_eval must return -1
              without consulting PyErr or out_ev.
*/
typedef enum KosOpResultKind {
  KOS_RES_NORMAL,
  KOS_RES_REPROLOGUE,
  KOS_RES_YIELD,
  KOS_RES_AWAIT,
  KOS_RES_RETURN,
  KOS_RES_RAISE,
  KOS_RES_CONTROL_RETURNED,
  KOS_RES_FATAL,
} KosOpResultKind;

typedef struct KosOpResult {
  KosOpResultKind kind;
  PyObject       *val;   /* strong ref; NULL except for YIELD/AWAIT/RETURN */
} KosOpResult;
```

Macros replacing the current `KOS_OP_*` family:

```c
#define EVAL_NORMAL()            ((KosOpResult){ KOS_RES_NORMAL,            NULL })
#define EVAL_REPROLOGUE()        ((KosOpResult){ KOS_RES_REPROLOGUE,        NULL })
#define EVAL_YIELD(v)            ((KosOpResult){ KOS_RES_YIELD,             (v)  })
#define EVAL_AWAIT(v)            ((KosOpResult){ KOS_RES_AWAIT,             (v)  })
#define EVAL_RETURN(v)           ((KosOpResult){ KOS_RES_RETURN,            (v)  })
#define EVAL_RAISE()             ((KosOpResult){ KOS_RES_RAISE,             NULL })
#define EVAL_CONTROL_RETURNED()  ((KosOpResult){ KOS_RES_CONTROL_RETURNED,  NULL })
#define EVAL_FATAL()             ((KosOpResult){ KOS_RES_FATAL,             NULL })
```

The opcode signature becomes:

```c
KosOpResult op_NAME(KosEvalState *es, KosInsn *insn);
```

`out_ev` is removed from the opcode handler signature. It remains a parameter
to `koskari_eval` and to internal eval helpers, but opcodes never touch it.

---

## 4. The eval loop dispatch

Current hot path in the dispatch macro:

```c
#define OP(name, ...)                                              \
  cg_TARGET_##name:                                               \
    op_out = op_##name(es, out_ev, &insn);                        \
    if (likely(op_out.mode == KOS_LOP_RUN)) {                     \
      /* ... GIL poll ... */                                       \
      goto *optable[next_insn.op];                                 \
    }                                                              \
    goto cg_handle_op_outcome;
```

Proposed:

```c
#define OP(name, ...)                                              \
  cg_TARGET_##name:                                               \
    op_res = op_##name(es, &insn);                                 \
    if (likely(op_res.kind == KOS_RES_NORMAL)) {                  \
      /* ... GIL poll ... */                                       \
      goto *optable[next_insn.op];                                 \
    }                                                              \
    goto cg_handle_op_result;
```

`cg_handle_op_result` replaces `cg_handle_op_outcome`:

```c
cg_handle_op_result:
  switch (op_res.kind) {

  case KOS_RES_NORMAL:
    /* unreachable — caught by hot-path check above */
    goto cg_normal_dispatch;

  case KOS_RES_REPROLOGUE:
    goto cg_prologue;

  case KOS_RES_FATAL:
    return -1;

  case KOS_RES_RAISE:
    /* PyErr is already set; run the raise path */
    {
      KosEvalStep rs = eval_event_raise(es, out_ev);
      if (rs == KOS_STEP_FATAL_ERROR) return -1;
      if (rs == KOS_STEP_STOP_EVENT)  return 0;
      goto cg_prologue;
    }

  case KOS_RES_YIELD:
    /* Demand check + walk + boundary delivery */
    {
      KosEvalStep ys = eval_event_yield(es, op_res.val, out_ev);
      if (ys == KOS_STEP_FATAL_ERROR) return -1;
      if (ys == KOS_STEP_STOP_EVENT)  return 0;
      goto cg_prologue;
    }

  case KOS_RES_AWAIT:
    /* Demand denial or suspension record */
    {
      KosEvalStep as = eval_event_await(es, op_res.val, out_ev);
      if (as == KOS_STEP_FATAL_ERROR) return -1;
      if (as == KOS_STEP_STOP_EVENT)  return 0;
      goto cg_prologue;
    }

  case KOS_RES_RETURN:
    /* Walk + pop + boundary or parent delivery */
    {
      KosEvalStep rs = eval_event_return(es, op_res.val, out_ev);
      if (rs == KOS_STEP_FATAL_ERROR) return -1;
      if (rs == KOS_STEP_STOP_EVENT)  return 0;
      goto cg_prologue;
    }

  case KOS_RES_CONTROL_RETURNED:
    {
      KosEvalStep cs = eval_event_control_returned(es, out_ev);
      if (cs == KOS_STEP_FATAL_ERROR) return -1;
      if (cs == KOS_STEP_STOP_EVENT)  return 0;
      goto cg_prologue;
    }
  }
```

The dispatch is now a single switch on a single field. The mapping from
semantic event to handler is explicit and in one place.

---

## 5. What the val ownership contract looks like

`KosOpResult.val` is a strong reference for YIELD, AWAIT, and RETURN; NULL
for everything else.

| Kind | val meaning | Owner before dispatch | Consumed by |
|---|---|---|---|
| NORMAL | NULL | — | — |
| REPROLOGUE | NULL | — | — |
| YIELD | yielded value | Stolen from stack by opcode | `eval_event_yield` steals or `Py_DECREF` |
| AWAIT | awaitable | New ref from opcode | `eval_event_await` steals or `Py_DECREF` |
| RETURN | return value | New ref from opcode | `eval_event_return` steals or `Py_DECREF` |
| RAISE | NULL | PyErr holds the exc | `eval_event_raise` reads PyErr |
| CONTROL_RETURNED | NULL | — | native hook |
| FATAL | NULL | — | — |

Rule: the dispatch switch owns `op_res.val` from the moment of the switch.
Every branch is responsible for exactly one of: stealing into a consumer,
`Py_DECREF` on error, or passing ownership to an `eval_event_*` helper
(which then follows the same rule recursively). The opcode itself must not
retain a reference after returning the result.

Failure mode to avoid: opcode calls `Py_INCREF(val)`, stores `val`
somewhere on the stack or in a local, AND puts `val` into `KosOpResult`.
That's a double reference that will only show as a leak in valgrind.

---

## 6. What happens to `out_ev`

`out_ev` has two roles today:

1. **Intra-eval side channel**: opcodes write to it to communicate with
   `eval_phase_post_opcode`. This role is entirely eliminated; opcodes no
   longer receive or touch `out_ev`.

2. **Driver-facing exit carrier**: when `koskari_eval` stops (`return 0`),
   `out_ev` holds the boundary event (YIELD payload, AWAIT awaitable,
   RETURN value, or RAISE exc_type) for the caller.

Role 2 remains. The eval helpers (`eval_event_yield`, `eval_event_return`,
etc.) write to `out_ev` immediately before returning `KOS_STEP_STOP_EVENT`.
`out_ev` is an output-only parameter for `koskari_eval`; it is never read by
opcode handlers.

The `KOS_EV_POST_OPCODE` event kind, which exists solely as an intra-eval
deferral mechanism (a `PyLong`-carrying `out_ev` entry that is drained at
the prologue to re-run `eval_phase_post_opcode`), is **eliminated**. See §9.

---

## 7. Native handler protocol change

Today, `KosDynFrame.native_handler` has the signature:

```c
int (*native_handler)(KosEvalState *, KosEvent *, int op_res, PyObject *val);
```

It returns `KOS_OP_RES_*` integers (0, 1, -1, `KOS_OP_RES_CONTROL_RETURNED`),
which `eval_parent_return_deliver_impl` then re-dispatches through
`eval_emit_post_opcode_deferred`.

Under the unified model, the native handler returns `KosOpResult`:

```c
KosOpResult (*native_handler)(KosEvalState *, PyObject *val);
```

The `KosEvent *` parameter goes away (handlers do not write to `out_ev`).
The `int op_res` parameter was context for which post-opcode path called the
handler; with a single dispatch path that context is no longer needed.

The return values map as:

| Old return | New KosOpResult |
|---|---|
| 0 (ok, continue) | `EVAL_REPROLOGUE()` |
| 1 (ok, with deferred suspend) | replaced by `EVAL_YIELD` / `EVAL_AWAIT` directly |
| -1 (error) | `EVAL_RAISE()` with PyErr set |
| `KOS_OP_RES_CONTROL_RETURNED` | `EVAL_CONTROL_RETURNED()` |

The call site in `eval_parent_return_deliver_impl` (which becomes part of
`eval_event_return`) becomes:

```c
if (KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es)) != NULL) {
  KosOpResult hr = KosDynFrame_NATIVE_HANDLER(KosEvalState_FRAME(es))(es, val);
  Py_DECREF(val);
  /* hr is now the result; dispatch it as any other result */
  return eval_dispatch_result(es, hr, out_ev);
}
```

`eval_dispatch_result` is the same logic as `cg_handle_op_result` above,
factored into a callable static. This eliminates `eval_emit_post_opcode_deferred`.

**Surface area:** All native handlers across the codebase must be updated.
Search `KosDynFrame_NATIVE_HANDLER` and `native_handler` assignments to find
every one. They are not numerous (WIND, WITH, a handful of dynchain callbacks)
but each requires a protocol update and its tests must be re-run.

---

## 8. Transfer plans and thunk execution

This is the area most likely to cause concern. The summary: transfer plans
and thunks do **not** require new encoding in `KosOpResult` because the
"are we in a thunk?" question is answered by context state, not by the
opcode result.

### 8.1 How transfer plans currently route

When `KOS_CTX_IN_TRANSFER` is set, the dispatch prologue (`cg_prologue`)
routes to `KOS_MODE_TRANSFER` instead of `KOS_MODE_NORMAL`. This runs
`kos_eval_step_process_plan`, which steps the transfer plan and installs the
next thunk frame. The opcode stream of the thunk then runs normally with the
standard result dispatch. The transfer flag is checked at prologue entry, not
during opcode execution.

This does not change with `KosOpResult`. The prologue still checks
`KOS_CTX_IN_TRANSFER` and still routes to the same transfer drain. Thunk
opcodes emit `EVAL_NORMAL()`, `EVAL_RETURN(val)`, etc. exactly as any other
opcode would. Nothing in `KosOpResult` needs to encode "this is a thunk."

### 8.2 Thunk RETURN → CONTROL_RETURNED path

When a thunk frame returns, `RETURN.c` emits `EVAL_RETURN(val)`.
`eval_event_return` checks for a native handler on the frame (installed by
WIND/WITH when the thunk was set up). The native handler fires and returns
`EVAL_CONTROL_RETURNED()`. `eval_event_return` dispatches that to
`eval_event_control_returned`, which runs `kos_dynnode_on_control_returned`
then continues. No new encoding needed; the frame structure already carries
the "I am a thunk" information via the installed handler.

The critical invariant: `eval_event_return` consults the dynchain *after*
calling `kos_dyn_pop_frame`, so the frame pointer is to the *caller* frame
when checking for a native handler. This matches current behavior in
`eval_parent_return_deliver_impl`.

### 8.3 Exception in a thunk

If a thunk raises, the opcode returns `EVAL_RAISE()` (PyErr set). The
dispatch switch calls `eval_event_raise`. That function runs
`try_local_exception_handler`, which walks the chain and may find a
FINALLY or error boundary. No special encoding needed.

### 8.4 Transfer plan producing a suspension

If a thunk somehow produces `EVAL_YIELD` or `EVAL_AWAIT`, those go through
the same `eval_event_yield` / `eval_event_await` paths as any other yield or
await. The demand guard in `eval_event_await` would fire if the context is
under demand extent. The semantics are identical regardless of whether the
yield comes from a thunk or a regular frame.

### 8.5 Summary

Transfer plan execution is transparent to the opcode result protocol. The
prologue's `KOS_CTX_IN_TRANSFER` check, the dynchain structure, and the
native handler slot on frames together carry all "thunk context" information.
`KosOpResult` does not need a THUNK_OP variant or any special flag.

---

## 9. Eliminating `KOS_EV_POST_OPCODE`

`KOS_EV_POST_OPCODE` is an eval-internal event kind that exists to defer a
second run of `eval_phase_post_opcode`. It arises in one situation: when a
native handler (called during `eval_parent_return_deliver_impl`) returns
`KOS_OP_RES_CONTROL_RETURNED`, the code cannot call `eval_phase_post_opcode`
recursively, so it writes `KOS_EV_POST_OPCODE` with a `PyLong` payload into
`out_ev` and returns `KOS_STEP_REPROLOGUE`. The prologue then drains this
before the next opcode.

In the unified model:

- Native handlers return `KosOpResult` directly.
- `eval_event_return` calls the native handler and receives `KosOpResult`.
- That result is dispatched by `eval_dispatch_result` immediately — no
  recursion, because `eval_event_return` itself is already a called helper
  (not the dispatch switch). `eval_dispatch_result` calls whatever eval event
  helper corresponds to the result kind.
- The prologue re-runs via `KOS_STEP_REPROLOGUE` from `eval_dispatch_result`,
  same as always.

There is no scenario where `KOS_EV_POST_OPCODE` is needed. The `PyLong`
heap allocation it causes on every native-handler-driven return also goes away.

---

## 10. What collapses vs what stays

### 10.1 Genuine structural collapses

**`KosOpOutcome` → `KosOpResult`**: The `KosLop` mode field and `post_tag`
sub-field collapse into a single `KosOpResultKind`. The translation function
`kos_op_post_tag_to_res` disappears. `KOS_OP_RES_*` integer defines disappear.

**Two output channels → one**: Opcodes no longer receive or touch `out_ev`.
The implicit coupling "if you set out_ev, you must return POST_SUSPEND" is
replaced by an explicit encoding in the result struct kind. Type-checker
enforcing becomes possible; today it cannot be.

**`eval_phase_post_opcode` + `eval_phase_post_opcode_tail`**: These two
functions currently translate `op_res` → handler. They go away; the dispatch
switch is the translation.

**`KOS_EV_POST_OPCODE`**: Eliminated. See §9. The PyLong allocation it causes
goes away.

**Native handler protocol**: The `int` return with `KOS_OP_RES_*` integers
becomes typed `KosOpResult`. The `KosEvent *` parameter to native handlers
disappears.

### 10.2 What stays (not collapsible with this change)

**`KosEvalStep`**: The internal helper return type remains. `eval_event_yield`,
`eval_event_return`, etc. still return `KosEvalStep` to the dispatch switch,
which checks three values (FATAL_ERROR, STOP_EVENT, otherwise). This is a
small 3-way branch, not a cascade. It could theoretically be folded into
`KosOpResult` but the helpers do not have the same contract as opcodes (they
run after the result is dispatched), and keeping them separate avoids
confusion about ownership and call ordering.

**`KosEvalMode`**: The prologue entry modes (TRANSFER, EXCEPTION, RAISE,
AWAIT, YIELD, YIELD_FROM, RETURN) are the re-entry protocol — how
`koskari_eval` resumes a suspended context. This is the mirror of the exit
protocol. It is not the same as `KosOpResult` (which is the in-loop signal
from opcodes). It stays as-is; re-entry mode derivation from `KosSuspensionEvent`
and context flags is unchanged.

**`KosSuspensionEvent`**: Recorded on context for the driver to know why eval
stopped. Remains as the persistent suspension record; `KosOpResult` is the
transient in-loop signal.

**`KosEvent` / `out_ev`**: Remains as the driver-facing exit carrier, written
only by eval helpers immediately before `STOP_EVENT` return. Not touched by
opcodes.

**`kos_dyn_transition`, dynchain, transfer plans**: Unchanged. These operate
on context state, not on opcode results.

### 10.3 Honest risk assessment: improvement or renaming?

**Genuine performance improvement:** Marginal on the hot path. The hot-path
check today is `op_out.mode == KOS_LOP_RUN`; the new check is
`op_res.kind == KOS_RES_NORMAL`. Both are a single integer compare. On the
off-path (any suspension or boundary event), the current cascade
(translate tag → call post_opcode → check op_res → call post_opcode_tail →
check event kind → call helper) becomes a single switch dispatch to the right
helper. This is a measurable improvement for suspension paths, which are
critical for yield-heavy code, but not for the arithmetic/call hot path.

**Genuine maintenance improvement:** The two-channel convention is the most
common source of confusion when reading or modifying opcode handlers. A
reviewer must mentally track both the `KosOpOutcome` return and the `out_ev`
mutation and know they are coupled. Making the contract one struct with one
ownership rule is a real clarity improvement.

**Real risk — not renaming:** The native handler protocol change has wide
surface area and affects WIND, WITH, and several dynchain callbacks. Each one
must be updated and its tests verified. The protocol is not mechanically
translatable in all cases (the old `int res == 1` case that calls
`eval_emit_post_opcode_deferred(out_ev, 1)` has no direct analogue and must be
reasoned about individually).

**Risk that it is partly renaming:** `KosEvalStep`, `KosEvalMode`,
`KosSuspensionEvent`, and `KosEvent` remain. The total number of types
encoding "what is the eval loop doing" goes from eight to five. If the goal
is a single unified type for all of them, this does not achieve it. The
reduction is real but not complete.

**Risk of correctness regression:** `KosOpResult.val` ownership across the
dispatch switch boundary is a new invariant that every opcode author must
know. Currently, opcodes mutate `out_ev` in-place and the ownership of the
payload is governed by the event kind; that rule already exists and is
documented. Moving ownership into the return struct is not harder, but it is
different, and any opcode that currently stores a value in both `out_ev` and a
local must be carefully reviewed.

**The RETURN migration dependency:** The full benefit of this change is
realized only if `RETURN.c` Phase 2b/2c is simultaneously migrated into
`eval_event_return` (as described in
[event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md)
§12). If `KosOpResult` is introduced but `RETURN.c` still runs its own walk
and pop, then RETURN's result kind is `EVAL_RETURN(val)` but `eval_event_return`
receives a frame already popped by the opcode — a latent ordering violation.
**These two migrations must be done together or not at all for RETURN.**

---

## 11. Implementation phases

### Phase 0 — Define `KosOpResult` in parallel; no behavior change

Add `KosOpResult`, `KosOpResultKind`, and `EVAL_*` macros to `eval.h`.
Add a static `eval_dispatch_result(es, res, out_ev)` that translates a
`KosOpResult` to a `KosEvalStep` by calling the appropriate `eval_event_*`
helper.

Do not change any opcode. Run `tox -qe py` to confirm no compilation change.

### Phase 1 — Convert NORMAL-only opcodes (bulk, mechanical)

Opcodes that today unconditionally return `KOS_OP_RUN()` and never touch
`out_ev` (CONST, VAR, VAR_STORE, VAR_REF, OBJECT_ADD, FAST, POP, DUP, and
most arithmetic opcodes) switch to the new signature and return
`EVAL_NORMAL()`. The eval loop OP macro is updated to call the new signature
for converted opcodes, or the OP macro is updated for all at once and all
opcodes are converted together.

Since these opcodes only hit the hot path, no behavior changes. Test after
each batch.

### Phase 2 — Convert RAISE and ERR opcodes

Opcodes that call `KOS_OP_POST_ERR()` after setting `PyErr`. These become
`EVAL_RAISE()`. The `eval_phase_post_opcode_tail` PyErr branch becomes the
`KOS_RES_RAISE` switch case.

### Phase 3 — Convert YIELD and AWAIT opcodes

`YIELD.c`: remove `KosEvent_KIND(out_ev) = KOS_EV_YIELD` and
`KosEvent_PAYLOAD(out_ev) = val`; instead `return EVAL_YIELD(val)`.
`AWAIT.c`: same. The `eval_event_yield` and `eval_event_await` (new helper
factored from the current AWAIT handling in `eval_phase_post_opcode_tail`)
now receive val as a parameter instead of reading from `out_ev`.

The two YIELD_FROM early-exit guard sites that call
`kos_eval_demand_deny_suspend` remain; they precede event emission and do not
touch `out_ev`.

### Phase 4 — Convert RETURN and migrate Phase 2b/2c simultaneously

This is the largest phase and must be done atomically. `RETURN.c` Phase 2b/2c
(transition, async-unwind, pop, and parent delivery) moves into
`eval_event_return(es, val, out_ev)`. `RETURN.c` becomes:
Phase 1 (operand pop) + optional Phase 2a (delimited return, which may keep
its current opcode-local path) + `return EVAL_RETURN(val)`.

The `KOS_OP_POST_PARENT_RETURN()` macro and `eval_parent_return_deliver` are
subsumed into `eval_event_return`.

Run NEXT, ITER_NEXT, FOREACH, YIELD_FROM, try/finally, and native return
tests before and after.

### Phase 5 — Update native handler protocol

Change `native_handler` typedef from `int (*)(..., KosEvent *, int, PyObject *)`
to `KosOpResult (*)(KosEvalState *, PyObject *)`. Update all callsites:
WIND handler, WITH handler, and any others found by grep. Update
`eval_parent_return_deliver_impl` (now part of `eval_event_return`) to call
the new protocol and dispatch the result via `eval_dispatch_result`.

### Phase 6 — Eliminate `KOS_EV_POST_OPCODE`

Remove `KOS_EV_POST_OPCODE` from `KosEventKind`. Remove
`eval_emit_post_opcode_deferred`. Remove the `KOS_EV_POST_OPCODE` drain from
the dispatch prologue. Verify with valgrind that no PyLong leak is introduced
or removed unexpectedly.

### Phase 7 — Remove `eval_phase_post_opcode` and `eval_phase_post_opcode_tail`

These functions are now empty (all their branches are in the dispatch switch
or in `eval_dispatch_result`). Delete them. Remove `kos_op_post_tag_to_res`.

### Phase 8 — Clean up `KosOpOutcome` and old macros

Delete `KosOpOutcome`, `KosLop`, `KOS_OP_POST_TAG_*`, `KOS_OP_RES_*`,
`KOS_OP_RUN`, `KOS_OP_POST_SUSPEND`, `KOS_OP_POST_ERR`, `KOS_OP_POST_CONTROL`,
`KOS_OP_POST_PARENT_RETURN`, `KOS_OP_PROLOGUE`, `KOS_OP_FAIL`.

### Phase 9 — Full validation

`make tox`, `make test-valgrind`. Audit YIELD_FROM nested eval (§12 below).
Check continuation capture tests. Confirm GIL poll behavior is unchanged
(it is tied to the hot-path check, not to post-opcode logic).

---

## 12. YIELD_FROM and nested `koskari_eval` calls

`YIELD_FROM.c` calls `koskari_eval` directly and reads `out_ev` to decide
what to do with the result of the inner eval. This does not change because
`out_ev` (the driver-facing exit carrier, role 2 from §6) is unaffected.
When `koskari_eval` returns 0, `out_ev` still contains the event. YIELD_FROM
reads `KosEvent_KIND(out_ev)` as today.

However: once `RETURN.c` Phase 2c is migrated into `eval_event_return`, the
inner eval no longer emits `KOS_EV_RETURN` from the opcode for the
root/GEN-parent path. Instead `eval_event_return` emits it before returning
`KOS_STEP_STOP_EVENT`. The `out_ev` is still set correctly; YIELD_FROM sees
the same event. The point to audit is that `eval_event_return` correctly
distinguishes between the GEN-parent path (emit terminal event, mark dead)
and the non-GEN-parent path (deliver to parent, reprologue), which is a
classification step that is currently in RETURN.c's phase-2b/2c
if-ladder and must be faithfully replicated in `eval_event_return`.

---

## 13. Edge cases

1. **RETURN during async unwind** (`KOS_DYN_UNWIND_ASYNC_STARTED`): Today
   `RETURN.c` calls `KosEvalState_SetPendingReturn(es, val, is_nested)` and
   returns `KOS_OP_RUN()`. In the unified model, `eval_event_return` calls
   `SetPendingReturn` and returns `KOS_STEP_REPROLOGUE`. The opcode's
   `EVAL_RETURN(val)` result is consumed by `eval_event_return` before
   `SetPendingReturn`; `val` is handed to the pending-return mechanism which
   owns the reference. No double-free as long as `SetPendingReturn` steals val.

2. **RETURN during finally-taking-return**: Transition result
   `KOS_DYN_UNWIND_FINALLY_TAKING_RETURN` causes `eval_event_return` to return
   `KOS_STEP_REPROLOGUE` without popping the frame. Same as today but inside
   the helper instead of the opcode.

3. **Continuation capture mid-return**: If a continuation is captured inside
   a `finally` block that was triggered by a RETURN, the continuation's capture
   point is inside `eval_event_return`. This is fine; continuation capture
   operates on the context's dynchain, not on the call stack of eval helpers.
   The frame is still live (not popped) at the time finally executes, which
   must remain true in `eval_event_return`'s unwind sequence.

4. **Demand AWAIT interception**: Currently in `eval_phase_post_opcode_tail`.
   In the unified model, the `KOS_RES_AWAIT` switch case calls
   `eval_event_await(es, val, out_ev)` which checks `kos_eval_in_demand(es)`,
   clears `val` (releases the awaitable), and calls `eval_event_raise`. The
   `Py_DECREF` on val that currently appears in the demand path (clearing the
   payload before redirecting to raise) must move to `eval_event_await` before
   calling `eval_event_raise`.

5. **`KOS_RES_NORMAL` from an opcode that already encountered PyErr**: If an
   opcode calls a Python C API that sets PyErr but then returns `EVAL_NORMAL()`
   (a bug), the current hot path would proceed to the next instruction without
   noticing. This is the same behavior as today for `KOS_LOP_RUN()`. The
   prologue's PyErr check (`cg_check_signals`) eventually catches it on the
   next GIL poll, same as now. No regression.

6. **`eval_dispatch_result` called from `eval_event_return` (re-entrancy)**:
   `eval_event_return` may call a native handler, receive a `KosOpResult`,
   and call `eval_dispatch_result`. That function calls another eval helper
   (e.g. `eval_event_control_returned`), which may call `koskari_eval`
   (for wind thunks). This is the same call depth as today. It is not
   recursive in the dangerous sense because each koskari_eval entry uses a
   fresh stack frame, but the C stack depth during transfer-plan execution
   should be profiled.

7. **Opcodes with multiple code paths**: An opcode that sometimes returns
   normally and sometimes suspends (e.g. CALL opcodes that sometimes push
   directly and sometimes install a frame) must cover all branches. Where it
   used to return `KOS_OP_RUN()` in one branch and `KOS_OP_PROLOGUE()` in
   another, it now returns `EVAL_NORMAL()` and `EVAL_REPROLOGUE()`
   respectively. The `val` field must be NULL in both.

8. **YIELD_FROM's own early-exit guard sites**: The two sites that call
   `kos_eval_demand_deny_suspend` (which internally sets PyErr and returns 1)
   precede event emission. In the new model, when a site detects demand denial
   it should return `EVAL_RAISE()` (PyErr already set by
   `kos_demand_set_sync_violation`). These sites do not set `out_ev` today;
   they set PyErr and return `KOS_OP_POST_ERR()`. The conversion is direct.

9. **GIL poll accounting**: The hot-path `gil_poll_ops` counter increments on
   every `KOS_RES_NORMAL` result. This is unchanged from the `KOS_LOP_RUN`
   path today. Results other than NORMAL reset to the prologue or stop;
   neither case needs the counter incremented.

---

## 14. Success criteria

- `KosOpOutcome`, `KosLop`, `KOS_OP_POST_TAG_*`, `KOS_OP_RES_*`, and the
  old `KOS_OP_*` macros are absent from the tree.
- `KOS_EV_POST_OPCODE` is absent from `KosEventKind`.
- `eval_phase_post_opcode` and `eval_phase_post_opcode_tail` are absent.
- `kos_op_post_tag_to_res` is absent.
- `out_ev` does not appear in any opcode handler signature.
- Opcode handlers may not read or write `out_ev` (enforce via convention or
  by not passing it; the latter requires removing the parameter from the OP
  macro invocation).
- `eval_event_yield`, `eval_event_await`, `eval_event_return`, and
  `eval_event_raise` each take the semantic value (`PyObject *val` or nothing)
  as a parameter rather than reading it from `out_ev`.
- Full `tox` green. Native `make test-valgrind` green with baseline leak
  unchanged. YIELD_FROM, try/finally, continuation, native-return, and
  demand-sync tests all pass.

---

## Document history

- **2026-06-22** — Initial draft: vision, current layers, `KosOpResult`
  proposal, eval loop dispatch, val ownership, out_ev fate, native handler
  protocol, transfer plan analysis, POST_OPCODE elimination, implementation
  phases, edge cases, honest risk assessment.
- **2026-06-22** — Supplementary §15–§19 added: Phases 0–4 landing record,
  deviations from plan, remaining work plan (Steps 1–4), and implementation
  notes for Phase 5 native handler migration.

---

## 15. Landing record: Phases 0–4 (commit e5c37cc, branch eval_event_again)

Phases 0–4 from §11 are complete as of commit `e5c37cc`. This section records
what was implemented, deviations from the design, and bugs caught in testing.

### 15.1 What was implemented

**`eval.h`** — `KosOpResultKind`, `KosOpResult`, and all `EVAL_*` macros added.
`KOS_OP_HANDLER` macro updated to new signature `KosOpResult op_##name(KosEvalState
*es, KosOp *insn)`. Old `KosOpOutcome` infrastructure left in place (legacy,
still used by native deferral path — see §16).

**`eval_utils.c`** — Added alongside the existing `kos_op_outcome_*` helpers:
- `kos_op_result_from_push_int(int r)` — maps 0/-1 to NORMAL/RAISE
- `kos_op_result_PUSH(es, val)` — stack push without auto-await (val ownership
  transfers to stack; RAISE on failure)
- `kos_op_result_from_int_step(int r, KosEvent *ev)` — maps the legacy int-step
  protocol to `KosOpResult`; for r==1 discriminates YIELD vs AWAIT via
  `ev->kind`; for r==4 (`KOS_OP_RES_PARENT_RETURN_DELIVER`) extracts payload
  and returns `EVAL_PARENT_RETURN(val)`; clears `ev->payload` on exit to prevent
  double-free
- `kos_op_result_PUSH_OR_AWAIT(es, val)` — uses an internal scratch `KosEvent`
  and `op_push_or_await`; returns NORMAL on push, AWAIT(awaitable) on suspend,
  RAISE on error

**`eval.c`** — `kos_op_result_from_call_int(es, r, ev)` added (mirrors
`kos_op_outcome_from_call_int`). `eval_event_yield` updated to take `PyObject
*val` directly (centralises `FlushLiveRegsToFrame` + `MarkDeadAfterYield` that
were previously scattered across YIELD.c and YIELD_FROM's emit path).
`eval_event_await` added (demand check, `MarkSuspendedNotRunning`, fill out_ev,
`SetSuspensionEvent`). `eval_event_return` added (owns Phase 2b/2c: Transition
walk, async-unwind branch, pop-and-parent-deliver, agen-boundary check, and
root-frame mark-dead). `cg_handle_op_result` label and switch added; `OP` macro
updated to check `op_res.kind == KOS_RES_NORMAL` on the hot path. `kos_check_*`
helpers (`kos_check_contains`, `kos_check_identity`, `kos_check_two_arg`,
`kos_check_cmp`) had their unused `KosEvent *out_ev` parameter removed.

**All 215 opcode `.c` files** converted: no opcode file now receives or
touches `out_ev`. Every file ends with `// The end.` per house C style (many
were missing it before).

### 15.2 Deviations from the design doc

**`KOS_RES_PARENT_RETURN` added** (not in original §3 enum). The CALL family
can result in a frame already popped with a value ready to deliver to the
parent — this is distinct from `KOS_RES_RETURN` (which enters `eval_event_return`
and does the walk/pop itself). The new kind maps to `eval_parent_return_deliver`
directly in `cg_handle_op_result`. `kos_op_result_from_call_int` and
`kos_op_result_from_int_step` both produce it.

**`eval_dispatch_result` not yet extracted** (proposed in §7 and §9). The
`cg_handle_op_result` switch has three repeated lines per non-NORMAL case:
```c
if (rs == KOS_STEP_FATAL_ERROR) return -1;
if (rs == KOS_STEP_STOP_EVENT)  return 0;
goto cg_prologue;
```
This helper was planned but not implemented in Phase 0–4 because the Phase 5
call site (inside `eval_event_return` calling a native handler) did not yet
exist. It must be extracted as the first step of Phase 5 work.

**`EVAL_FATAL()` is defined but never produced.** Every opcode error path
goes through `EVAL_RAISE()` (PyErr set). `KOS_RES_FATAL` is handled in the
switch (`return -1;`) but no opcode or helper currently emits it. Decision
deferred; see §17.2.

**Phases 1–4 merged into one atomic commit** rather than separated by batch.
The `KOS_OP_HANDLER` macro signature change forced atomicity across all 215
files. Tests were run after the full conversion, not after each batch.

**`kos_op_result_PUSH` added** (not in original design). Needed by opcodes
that push a value without auto-await (plain stack ops). Complement to
`kos_op_result_PUSH_OR_AWAIT`.

**`KOS_RES_NORMAL: break;` is unreachable** in `cg_handle_op_result`. The hot
path `likely(op_res.kind == KOS_RES_NORMAL)` branches away before the switch.
The arm was left in for completeness but should be removed or replaced with
`__builtin_unreachable()` / an assert — see §17.1.

### 15.3 Bug found during testing

**`eval_event_return` Phase 2c missing boundary delivery check.** The "root
frame / GEN parent" branch set `out_ev = KOS_EV_RETURN`, called
`MarkDeadAfterReturn`, and returned `KOS_STEP_STOP_EVENT` without checking
`kos_dyn_need_kos_agen_boundary_deliver`. The old code reached this check via
`eval_phase_post_opcode_tail`'s `KOS_EV_RETURN` branch. Flattened Koskari async
generators driven by ITER_NEXT or YIELD_FROM returned `None` instead of their
actual values.

Fix: after `MarkDeadAfterReturn`, check the boundary flag and dispatch to
`eval_event_return_deliver_to_boundary` if set, before returning
`KOS_STEP_STOP_EVENT`. Added one `if` before the final return in
`eval_event_return`.

Affected tests: `test_foreach_iter_next_over_koskari_async_gen`,
`test_koskari_foreach_over_koskari_generator`, all `test_yield_from_*` tests
involving Koskari async gen delegates, and one reproducer. All pass after fix.

### 15.4 Known hybrid state (intentional, documented for Phase 5–6)

The following old-protocol infrastructure remains active. **Do not add new call
sites to any of these.** They are kept only to support the native-handler
deferred path until Phase 5 converts it.

| Symbol | Location | Why still needed |
|---|---|---|
| `KosOpOutcome`, `KosLoopOpMode`, `KOS_LOP_*` | `eval.h` | Returned by `kos_op_outcome_from_call_int` (dead from opcodes; still defined) |
| `KOS_OP_RUN/POST_ERR/PROLOGUE/FAIL/POST_SUSPEND/POST_PARENT_RETURN` macros | `eval.h` | Used by `kos_op_outcome_*` helpers in eval_utils.c |
| `kos_op_outcome_from_call_int` | `eval.c` | Dead (no callers post-conversion); delete in Phase 6 |
| `kos_op_outcome_PUSH`, `kos_op_outcome_PUSH_OR_AWAIT`, `kos_op_outcome_from_push_int`, `kos_op_outcome_from_int_step` | `eval_utils.c` | Dead (no callers post-conversion); delete in Phase 6 |
| `eval_phase_post_opcode`, `eval_phase_post_opcode_tail` | `eval.c` | Active: called from `eval_phase_post_opcode` which is called from `eval_dispatch_maybe_post_opcode_event` (the POST_OPCODE drain) and from `kos_eval_post_opcode_deferred_step` |
| `eval_emit_post_opcode_deferred` | `eval.c` | Active: called by `eval_parent_return_deliver_impl` to defer a second post-opcode pass |
| `KOS_EV_POST_OPCODE` | `KosEventKind` | Active: written by `eval_emit_post_opcode_deferred`, drained by `eval_dispatch_maybe_post_opcode_event` in the dispatch prologue |
| Demand AWAIT denial in `eval_phase_post_opcode_tail` | `eval.c` | Active for native-path AWAIT events; parallel copy of the check in `eval_event_await` |

---

## 16. Remaining work: detailed plan

### Step 1 — Minor housekeeping ✓ DONE (branch eval_event_again, post-e5c37cc)

All three sub-tasks implemented and tested (3212 passed, 10 skipped, 24
subtests passed — same baseline as Phase 0–4 landing).

#### 1a. Remove dead `case KOS_RES_NORMAL:` from `cg_handle_op_result` ✓

Done: `case KOS_RES_NORMAL:` now returns `-1` with a comment "Unreachable:
hot path branches away before cg_handle_op_result." The trailing `return
eval_ret;` at switch bottom also replaced with `return -1;` and an
"Unreachable" comment.

#### 1b. Wire `EVAL_FATAL()` as a distinct internal-fault path ✓

Done:
- `case KOS_RES_FATAL:` in `cg_handle_op_result` now calls `SAY(...)` before
  `return -1`.
- `default:` in `cg_handle_op_result` now calls `SAY(...)` before `return -1`.
- `eval_dispatch_op_result` `case KOS_RES_FATAL:` and `default:` both call
  `SAY(...)` before returning `KOS_STEP_FATAL_ERROR`.
- `kos_op_result_from_int_step` `default:` branch changed from `EVAL_RAISE()`
  to `EVAL_FATAL()` — an unexpected int-step value is a structural fault, not
  a recoverable PyErr condition.

#### 1c. Extract `eval_dispatch_result` helper ✓

Done. Two forms extracted:

**`KOS_EVAL_DISPATCH_STEP(step)` macro** — defined locally in `eval.c` just
before the eval function body, `#undef`-ed at the end. Used in every
non-trivial case of `cg_handle_op_result` to replace the repeated 3-liner.
Each traced case now declares a local `KosEvalStep _s`, calls
`KOS_TRACE_POST_OP_WRAP(name, _s = fn(...))`, then `KOS_EVAL_DISPATCH_STEP(_s)`.
Untraced cases pass the call directly.

**`eval_dispatch_op_result(es, out_ev, res)` static function** — defined in
`eval.c` immediately before `eval_event_return`. Marked
`__attribute__((unused))` until Phase 5 wires it. Mirrors the
`cg_handle_op_result` switch but returns `KosEvalStep` instead of using
`goto`, for use from functions that cannot `goto cg_prologue`. Treats
`KOS_RES_NORMAL` and `KOS_RES_REPROLOGUE` identically (both return
`KOS_STEP_REPROLOGUE` — see §17.4 for rationale).

Forward declarations of both `kos_eval_post_opcode_control_returned_after_hook`
and `eval_dispatch_op_result` added to the forward-decl block near line 140.

---

### Step 2 — Phase 5: native handler protocol migration ✓ DONE (branch eval_event_again, post-05c2080)

All sub-tasks implemented and tested (3212 passed, 10 skipped, 24 subtests passed — same baseline).

**What was done:**
- `dynchain.h`: `native_handler` field changed from typed function pointer to `void *` (avoids include-order constraint on `KosOpResult`)
- `eval.h`: `KosNativeHandlerFn` typedef added after `KosOpResult` definition
- `eval.c`: `kos_eval_call_native_handler` replaced by `kos_eval_invoke_native_handler(es, val)` returning `KosOpResult`; `val` ownership transferred to handler (NULL for iteration call)
- `eval.c` `eval_phase_entry` resume delivery: now calls `kos_eval_invoke_native_handler` + `eval_dispatch_op_result`; REPROLOGUE → fall through to `*mode_out = mode; return KOS_STEP_CONTINUE` (proceeds to dispatch)
- `eval.c` `eval_parent_return_deliver_impl`: steals val from out_ev (clear without decref), calls `kos_eval_invoke_native_handler(es, val)`, returns `eval_dispatch_op_result` result — no deferred path
- `eval.c` `eval_step_native_iteration`: uses `kos_eval_invoke_native_handler(es, NULL)`, dispatches via `eval_dispatch_op_result`
- `eval.c` `eval_emit_post_opcode_deferred`: deleted
- `eval.c` `eval_dispatch_maybe_post_opcode_event`: deleted
- `eval.c` `eval_phase_post_opcode`: deleted (forward decl removed too; was the only entry point for the deferred path)
- `eval.c` prologue: `eval_dispatch_maybe_post_opcode_event` block removed
- `utils.c` `apply_native_handler`: signature changed to `KosOpResult fn(KosEvalState *, PyObject *)`; `delivered_value != NULL` errors with EVAL_RAISE; Transition uses local `_scratch_ev`; PushOrAwait dispatched manually with `_push_ev` scratch event
- `utils.c` `kos_install_apply`: cast changed from typed NativeHandlerFn to `(void *)`

**What remains after Phase 5 (Phase 6 work):**
- `eval_phase_post_opcode_tail` still exists and is called by `kos_eval_post_opcode_control_returned_after_hook` (for the CONTROL_RETURNED path through dynchain hooks). Phase 6 will either inline it there or refactor.
- `KosOpOutcome`, `KOS_OP_*` macros, dead `kos_op_outcome_*` helpers: deferred to Phase 6 (Step 3).

**Note on `eval_phase_post_opcode_tail` still being alive:**
The design doc (§16 Step 2d) said "remove `eval_phase_post_opcode_tail`", but this was premature — `kos_eval_post_opcode_control_returned_after_hook` calls it for the CONTROL_RETURNED continuation. This is **not** the native-handler deferred path; it is the synchronous WIND/WITH hook completion path. `KOS_EV_POST_OPCODE` is fully gone; `eval_phase_post_opcode` is gone. `eval_phase_post_opcode_tail` survives as a helper for `kos_eval_post_opcode_control_returned_after_hook` only. Step 3 will assess whether to inline or delete.

#### 2a. Locate all native handlers

Run: `grep -rn "native_handler\|KosDynFrame_NATIVE_HANDLER\|NATIVE_HANDLER" koskari/lib/`

Expected hits: the WIND dynchain handler, the WITH dynchain handler, possibly
one or two others in `dynchain/`. The count is small (fewer than 10 total
assignments). Document every file before starting.

Current typedef (in `dynchain.h` or `eval.h` — verify exact location):
```c
typedef int (*KosNativeHandler)(KosEvalState *, KosEvent *, int op_res, PyObject *val);
```

Target typedef:
```c
typedef KosOpResult (*KosNativeHandler)(KosEvalState *, PyObject *val);
```

The `KosEvent *` parameter disappears (handlers do not write to `out_ev`).
The `int op_res` parameter disappears (it was context for which post-opcode
path called the handler; the unified dispatch provides the same information
via `KosOpResult.kind` that the handler itself returns).

`PyObject *val` remains — it is the return value being delivered to the
native handler (the thunk result or with-entry result).

#### 2b. Update `eval_parent_return_deliver_impl`

This is the sole call site of `eval_emit_post_opcode_deferred`. Find it:
`grep -n "eval_emit_post_opcode_deferred\|eval_parent_return_deliver_impl" koskari/lib/eval.c`

Current logic (approximate):
```c
if (KosDynFrame_NATIVE_HANDLER(frame)) {
  int r = KosDynFrame_NATIVE_HANDLER(frame)(es, out_ev, op_res_int, val);
  Py_DECREF(val);
  eval_emit_post_opcode_deferred(out_ev, r);
  return KOS_STEP_REPROLOGUE;
}
```

The deferred path is needed because `eval_parent_return_deliver_impl` is called
during `eval_parent_return_deliver`, which is itself called from inside a
`KosEvalStep`-returning path that cannot directly run the post-opcode tail
again. The `KOS_EV_POST_OPCODE` in `out_ev` causes the prologue to drain it
before the next opcode.

New logic (after Step 1c):
```c
if (KosDynFrame_NATIVE_HANDLER(frame)) {
  KosOpResult hr = KosDynFrame_NATIVE_HANDLER(frame)(es, val);
  Py_DECREF(val);
  return eval_dispatch_op_result(es, out_ev, hr);
}
```

`eval_dispatch_op_result` (defined in Step 1c) dispatches the native handler's
`KosOpResult` to the appropriate `eval_event_*` helper and returns a
`KosEvalStep`. This is returned directly up to `eval_event_return` →
`cg_handle_op_result` → `EVAL_DISPATCH_STEP` macro. No deferral needed.

#### 2c. Update each native handler implementation

For each handler found in 2a, change its signature and return type. The
translation for return values:

| Old `int` return | New `KosOpResult` |
|---|---|
| 0 (continue; prologue re-check) | `EVAL_REPROLOGUE()` |
| -1 with PyErr set | `EVAL_RAISE()` |
| `KOS_OP_RES_CONTROL_RETURNED` | `EVAL_CONTROL_RETURNED()` |
| 1 with AWAIT in `out_ev` (if any) | `EVAL_AWAIT(val)` — extract payload from local scratch ev |

The `out_ev` write is replaced with a return value: if the handler suspended
on an awaitable, it returns `EVAL_AWAIT(awaitable)` directly. The payload
must have its ownership transferred correctly (steal from wherever it was
allocated, put into the `KosOpResult.val` field — do not leave it also in a
local variable or a `KosEvent` that will be dropped).

If a handler previously wrote to `out_ev` to set a YIELD or AWAIT payload and
then returned 1, it now returns `EVAL_YIELD(val)` or `EVAL_AWAIT(val)`. No
scratch `KosEvent` needed inside the handler; the handler owns the payload
until it returns it in the struct.

#### 2d. Remove `eval_emit_post_opcode_deferred` and the prologue drain

Once `eval_parent_return_deliver_impl` no longer calls
`eval_emit_post_opcode_deferred`, and once no other site produces
`KOS_EV_POST_OPCODE`, remove:

1. `eval_emit_post_opcode_deferred` function definition
2. The `KOS_EV_POST_OPCODE` drain block in `eval_dispatch_maybe_post_opcode_event`
   (or the entire function if that was its only purpose)
3. The call to `eval_dispatch_maybe_post_opcode_event` in the dispatch prologue
4. `KOS_EV_POST_OPCODE` from the `KosEventKind` enum

Verify: `grep -rn "KOS_EV_POST_OPCODE\|post_opcode_deferred\|maybe_post_opcode" koskari/lib/` returns zero hits after deletion.

Also check `koskari/lib/iter.c` (or wherever the comment about `KOS_EV_POST_OPCODE`
exists that the review mentioned): update or remove the reference.

#### 2e. Remove demand-AWAIT duplication

After Step 2d, `eval_phase_post_opcode_tail` is no longer reachable from any
active path. At that point, the demand AWAIT check in `eval_phase_post_opcode_tail`
(the block checking `KosEvent_KIND(out_ev) == KOS_EV_AWAIT` and
`kos_eval_in_demand`) is dead. Do not modify it yet (it goes away in Step 3
with the full deletion), but note that the authoritative demand-AWAIT check is
the one in `eval_event_await`.

Also: `MarkSuspendedNotRunning` is called in both `push_or_await_common`
(before returning r==1) and in `eval_event_await` (which is called after
`kos_op_result_PUSH_OR_AWAIT` returns `EVAL_AWAIT`). This double-call is
idempotent but noisy. After Phase 5 is complete and the full call graph is
clear, remove the redundant call from whichever site is less authoritative.
The canonical location should be `eval_event_await` since it is the sole
centralized AWAIT handler.

---

### Step 3 — Phase 6: full deletion of legacy infrastructure ✓ DONE (branch eval_event_again, post-e2bd983)

All sub-tasks implemented and tested (3212 passed, 10 skipped — same baseline).

**What was done:**
- `eval.c`: `kos_op_post_tag_to_res` function deleted; `kos_op_outcome_from_call_int`
  function deleted; section-separator comment updated ("KosOpOutcome via KOS_OP_*"
  → "KosOpResult via EVAL_*")
- `eval.c` `eval_phase_post_opcode_tail`: inlined into its only caller
  (`kos_eval_post_opcode_control_returned_after_hook`) and deleted. The inlined
  version is semantically identical except the `op_res < 0 ? FATAL : STOP`
  final branch is simplified to unconditional `STOP_EVENT` (CONTROL_RETURNED == 2
  is always positive, so the FATAL branch was dead). Error message updated to
  "control_returned hook: non-zero result but no event".
- `eval_utils.c`: `kos_op_outcome_PUSH`, `kos_op_outcome_PUSH_OR_AWAIT`,
  `kos_op_outcome_from_push_int`, `kos_op_outcome_from_int_step` deleted.
  Stale comment referencing `eval_phase_post_opcode_tail` updated to name
  `eval_event_await / eval_event_yield`.
- `eval.h`: `KosLoopOpMode` enum (`KOS_LOP_*`), `KOS_OP_POST_TAG_*` defines,
  `KosOpOutcome` struct/typedef, `KOS_OP_RUN/PROLOGUE/FAIL/POST_SUSPEND/POST_CONTROL/
  POST_ERR/POST_PARENT_RETURN` macros all deleted. `KOS_EV_POST_OPCODE` removed
  from `KosEventKind` enum and its comment block. `KOS_OP_RES_CONTROL_RETURNED`
  and `KOS_OP_RES_PARENT_RETURN_DELIVER` retained (still active).
- `iter.c`: `case KOS_EV_POST_OPCODE:` arm removed from the agen step switch.

**Note on `KOS_OP_RES_CONTROL_RETURNED` / `KOS_OP_RES_PARENT_RETURN_DELIVER`:**
These `#define`s (values 2 and 4) are retained. They are used by
`kos_op_result_from_int_step`, `kos_op_result_from_call_int`, the `utils.c`
`apply_native_handler`, and the `eval_utils.c` `op_push_or_await_common`.
They are NOT part of the deleted `KosOpOutcome/KosLoopOpMode` surface — they
belong to the `KosEvalState_PushOrAwait` int-return protocol, which is still
active. The §16 Step 3 sub-task 7 ("Delete `KOS_OP_RES_*` integer defines") was
incorrect as written; those defines remain load-bearing.

---

### Step 4 — Documentation refresh

Four files need updates (per the code review):

**`design/interpreter/eval-result-unification.md` (this file):**
- Change the preamble from "This is a speculative design document" to "Phases
  0–4 are implemented (commit e5c37cc); Phases 5–6 remain."
- After Step 3, add a note that all phases are complete and this document is
  now a historical record.

**`design/interpreter/evaluator.md`:**
- Section describing `KosOpOutcome` / post-opcode tag flow is stale. Replace
  with a description of `KosOpResult`, `cg_handle_op_result`, and the
  `eval_event_*` helper contract.
- Native handler protocol section needs updating after Step 2.

**`design/api/opcode-access-and-koscontext-surface.md`:**
- References to `KOS_OP_POST_ERR` per-opcode counts. Update or remove — opcodes
  no longer use that macro.

**`design/interpreter/demand-evalex-phase1.md`:**
- References the `KOS_OP_POST_ERR` → demand denial chain. This is now
  superseded: demand denial for AWAIT is handled centrally in `eval_event_await`;
  demand denial for YIELD is handled by `kos_eval_demand_deny_suspend` guards
  in YIELD.c / YIELD_FROM.c which return `EVAL_RAISE()`. Update accordingly.

**`koskari/lib/eval.h`:**
- Once Step 3 removes `KosLoopOpMode` / `KosOpOutcome`, their comments go with
  them. The `KosOpResult` / `KosOpResultKind` comments should be the primary
  narrative for the opcode protocol.
- `KosOpResultKind` enum comment: add a note clarifying that `KOS_RES_PARENT_RETURN`
  was added post-design-doc (it is not in the original §3 proposal) and
  explain when it is produced (CALL family where frame is already popped on
  return, distinct from RETURN which enters `eval_event_return` with frame
  still live).

---

## 17. Implementation notes for Step 2 (Phase 5)

### 17.1 Finding all native handler assignments

The full surface area search:
```bash
grep -rn "native_handler\|KosDynFrame_SET_NATIVE_HANDLER\|NATIVE_HANDLER" \
     koskari/lib/ --include="*.c" --include="*.h"
```

Expected results include assignment sites in PUSH_WIND, PUSH_WITH,
PUSH_DYN_WITH (and their POP counterparts), and the typedef site. Each
assignment installs a function pointer; each pointed-to function must be
updated to the new signature.

### 17.2 `EVAL_FATAL()` decision (from Step 1b)

The review noted `KOS_RES_FATAL` / `EVAL_FATAL()` is defined but never
produced. The decision made in §17: **wire it as a non-PyErr internal fault.**
This is consistent with the design doc §3 description ("unrecoverable internal
error; koskari_eval must return -1 without consulting PyErr or out_ev").

Concrete sites to wire during Step 1b:
- `kos_op_result_from_int_step` default branch (unexpected int-step value)
- `cg_handle_op_result` default branch (unexpected `KosOpResultKind`)
- `eval_dispatch_op_result` default branch (same)

Do NOT use `EVAL_FATAL()` as a substitute for `EVAL_RAISE()` when PyErr is
set. The diagnostic value depends on the distinction being maintained.

### 17.3 Ownership invariant for native handler return

When a native handler returns `EVAL_AWAIT(awaitable)`, the `awaitable` is a
new reference that `eval_dispatch_op_result` will pass to `eval_event_await`.
`eval_event_await` either stores it in `out_ev` (on normal suspension) or
`Py_DECREF`s it (on demand denial). The handler must not also `Py_DECREF` it
or store it elsewhere after constructing the `KosOpResult`.

When a handler returns `EVAL_RAISE()`, it must have set `PyErr` before
returning. The `eval_dispatch_op_result` path for RAISE does not consult
`KosOpResult.val` (which must be NULL); it calls `eval_event_raise(es, out_ev)`
which reads PyErr.

### 17.4 `KOS_STEP_REPROLOGUE` vs `EVAL_REPROLOGUE()` in native handlers

Old handlers returning 0 caused `eval_emit_post_opcode_deferred(out_ev, 0)` →
prologue drain → `eval_phase_post_opcode(es, out_ev, 0)` → (no PyErr, no
interesting event) → `KOS_STEP_STOP_EVENT` (which returned 0 from koskari_eval)
or `KOS_STEP_REPROLOGUE` depending on `KosEvent_KIND(out_ev)`.

**Important:** In the new protocol, a native handler returning `EVAL_REPROLOGUE()`
will cause `eval_dispatch_op_result` to return `KOS_STEP_REPROLOGUE` to
`eval_event_return`, which returns that to `cg_handle_op_result`, which
`goto cg_prologue`s. This is the correct behavior for handlers that install a
new frame or transfer plan and want the prologue to re-run its checks.

A handler that simply wants to "continue normally" (no new frame, no event)
should return `EVAL_REPROLOGUE()` — not `EVAL_NORMAL()`. `EVAL_NORMAL()` is an
opcode-dispatch result (advances PC); it does not make sense from a handler
called inside `eval_event_return`. `eval_dispatch_op_result` should treat
`KOS_RES_NORMAL` as `KOS_RES_REPROLOGUE` in this context, or assert that it is
never produced by a native handler. Document the decision in the handler typedef
comment.

### 17.5 Test targets for Step 2

Run these test suites explicitly before and after Step 2 commits:

```bash
tox -qe py -- tests/lib/opcode/test_next.py
tox -qe py -- tests/lib/opcode/test_yield.py
tox -qe py -- tests/lib/opcode/test_yield_from.py
tox -qe py -- tests/lib/test_run_iter_adapters.py
tox -qe py -- tests/reproducers/
tox -qe py -- tests/lib/test_demand_sync.py
```

These cover: ITER_NEXT / YIELD_FROM async gen boundary delivery (where Phase 4
bug was found), demand-sync violation paths, and native-return driven
reproducers. If any of these regress, check `eval_event_return` and
`eval_dispatch_op_result` first.
