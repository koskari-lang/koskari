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
