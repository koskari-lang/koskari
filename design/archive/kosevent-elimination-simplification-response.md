# KosEvent elimination + evaluator simplification: combined plan

**Audience:** Agents implementing evaluator refactors (including the author of
[kosevent-elimination.md](kosevent-elimination.md)).

**Purpose:** Record feedback on the elimination design, state the complementary
simplification goals, and merge both into **one ordered changeset** that removes
`KosEvent` without leaving duplicate dispatch layers or unfinished boundary
architecture behind.

**Related:**

- [kosevent-elimination.md](kosevent-elimination.md) — type-system and API cleanup
  (value-returned `KosOpResult`, no `out_ev` threading)
- [boundary-control-reemit.md](boundary-control-reemit.md) — staged frame crossing
  vs parent delivery
- [event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md) —
  thin opcodes, eval-owned `eval_event_*` routing
- [generic-dynnode-protocol.md](generic-dynnode-protocol.md) — generic vs
  type-specific dyn API
- [archive/eval-result-unification.md](../archive/eval-result-unification.md) —
  landed `KosOpResult` / `EVAL_*` migration (context for current tree)

---

## 1. Executive summary

The elimination plan and the simplification plan are **compatible**. They target
different layers of the same problem:

| Layer | Elimination plan | Simplification plan |
|-------|------------------|---------------------|
| **Types / API** | Delete `KosEvent`; thread `KosOpResult` by return value | Same (no second host struct; `koskari_eval` returns `KosOpResult`) |
| **Call graph** | Unify call/install/push_or_await and dynchain async paths on `KosOpResult` | Same; remove `KOS_OP_RES_*` int bridges |
| **Eval loop shape** | Rewrite `cg_handle_op_result` with nested switches | Collapse to **`apply_kos_op_result`** (one apply function) |
| **Opcode responsibilities** | Implicitly helped (no `out_ev` in opcodes) | Explicitly **thin `RETURN`**; move Phase 2b/2c to `eval_event_return` |
| **Dynnode boundary** | Category B: deliver fns take `PyObject *val` | **`kos_dyn_deliver_boundary` returns `KosOpResult`** (hooks are producers too) |
| **Reasoning model** | Still `goto cg_prologue` + `KOS_RES_REPROLOGUE` | Optional **`KosPhase`** enum after elimination lands |

**Recommendation:** Treat elimination **Steps 1–9** as **Track A** (mechanical,
grep-verifiable). Run **Track B** (architectural) in the same branch where
cheap, or immediately after Step 6, so we do not cement duplicate dispatch
before thinning `RETURN`.

**Single completion story for the combined work:**

1. `grep KosEvent` clean in `koskari/lib/` (elimination criterion).
2. `RETURN.c` contains operand pop + delimited branch + `EVAL_RETURN(val)` only
   (boundary re-emit criterion).
3. One C function applies all non-`NORMAL` opcode/dyn effects inside the eval
   loop (`apply_kos_op_result`).
4. Full `tox -qe test` green after each track milestone.

---

## 2. Agreement with kosevent-elimination.md

The following elimination choices are **endorsed** and should stay in the
combined plan:

### 2.1 One value type, no pointer threading

`KosOpResult { kind, val }` is the internal bus. No replacement struct for
`KosEvent`. Callers return or relay; they do not pass `KosEvent *` for callees
to fill.

Extending this to **`koskari_eval` → `KosOpResult`** (Category F/G) is
**preferred** over materializing `KosEvent` at eval exit. Hosts (`coro.c`,
`iter.c`, `demand.c`, `YIELD_FROM.c`) switch on `res.kind` and take ownership
of `res.val`. That is simpler than keeping a parallel host struct.

### 2.2 RAISE invariant

**For every `KOS_RES_RAISE` path: `PyErr` is set on the thread.** No
`exc_type` field, no fetch into a struct and restore in the driver.
`kos_eval_prepare_boundary_raise` (flush regs, inject traceback, leave PyErr
set, mark dead) replaces `kos_eval_emit_raise_from_pyerr`.

### 2.3 Category C — call family on `KosOpResult`

Unifying `KosInstallFunc`, `KosEvalState_Call*`, `KosEvalState_PushOrAwait` on
`KosOpResult` removes the largest remaining int bridge. Opcode pattern becomes:

```c
return KosEvalState_Call(es, callable, args, nargs, kwnames, tail);
```

Delete `kos_op_result_from_int_step` and `kos_op_result_from_call_int`.

### 2.4 Category B — deliver functions take `PyObject *val`

Boundary delivery helpers should not parse a struct; they receive the value
directly. Kind is implicit in which helper is invoked.

### 2.5 Bottom-up implementation sequence

Steps 1–9 (prepare raise → deliver → dynchain → dead params → install →
eval_event_* → koskari_eval signature → drivers → delete types) is the right
dependency order.

---

## 3. Corrections and gaps in kosevent-elimination.md

These should be fixed **in the combined plan** (update elimination doc or
follow this response as normative where they conflict).

### 3.1 Category A vs Category E — `kos_dynnode_on_control_returned`

**Issue:** Category A lists `kos_dynnode_on_control_returned(es, node)` as
dropping `out_ev` with `int` return. Category E rewrites
`kos_eval_post_opcode_control_returned_after_hook` to return `KosOpResult`
without `out_ev`. Today the hook **writes meaningful boundary state** into
`out_ev` (WIND/WITH resume: AWAIT, YIELD, boundary RETURN). Dropping `out_ev`
without giving the hook a return channel leaves a hole.

**Combined-plan fix:** Do **not** treat `kos_dynnode_on_control_returned` as
dead-parameter removal. Upgrade it in **Step 3b** (with Category D):

```c
KosOpResult kos_dynnode_on_control_returned(KosEvalState *es, KosDynNode *node);
```

Semantics:

- `KOS_RES_NORMAL` / `KOS_RES_REPROLOGUE` — hook consumed or advanced state;
  eval continues per today
- `KOS_RES_AWAIT` / `KOS_RES_YIELD` / `KOS_RES_RETURN` — boundary signal from
  hook; **`apply_kos_op_result`** routes like opcode-produced effects
- `KOS_RES_RAISE` — PyErr set

Then `kos_eval_post_opcode_control_returned_after_hook` becomes a thin relay:

```c
return apply_kos_op_result(es, kos_dynnode_on_control_returned(es, node));
```

Remove the block that re-reads `KosEvent_KIND(out_ev)` and dispatches again.

### 3.2 “What stays” — `KOS_OP_RES_*` contradiction

Elimination doc §“What stays” lists `KOS_OP_RES_CONTROL_RETURNED` and
`KOS_OP_RES_PARENT_RETURN_DELIVER` as remaining, while Category C and Step 5
absorb and remove them.

**Combined-plan fix:** After Step 5, **delete** `KOS_OP_RES_*` defines from
`eval.h`. “What stays” should list only `KosOpResult` / `KOS_RES_*` / `EVAL_*`.

### 3.3 `KOS_DYN_UNWIND_ASYNC_STARTED` → `KOS_RES_AWAIT`

Direction is right; risk is high. Callers that today distinguish “async unwind
started” from “opcode await” may rely on side state (`pending_return_value`,
plan armed, WITH phase).

**Combined-plan fix:** Keep Category D, but add an explicit **regression
matrix** (see §6.3) before merging Step 3. Where async unwind is not
equivalent to a host-visible AWAIT, return `KOS_RES_REPROLOGUE` and keep
internal phase on the node — do not force AWAIT if semantics differ.

### 3.4 `KosEvalStep` vs terminal `KosOpResult`

Category F keeps `KosEvalStep` for phase helpers while `koskari_eval` returns
terminal `KosOpResult`. That is acceptable as an intermediate state.

**Optional Track C (post-elimination):** Map phase helpers to `KosOpResult`
(`REPROLOGUE` vs `FATAL` only) and delete `KosEvalStep`. Not required for
elimination grep criterion.

### 3.5 Dead type cleanup

Remove unused **`KosEvalResult`** / `KosEvalResultKind` from `eval.h` during
Step 9 if still present. Orthogonal to elimination but reduces confusion.

---

## 4. Simplification additions (Track B)

Track A (elimination) removes types. Track B makes the **eval story** easy to
reason about after types are clean.

### 4.1 Target mental model

> An evaluator **step** produces exactly one **`KosOpResult`**. The loop
> **applies** that result until the next step is “fetch and run opcode” or
> “return result to host.”

Opcode description for agents:

- `CALL` — install or Python call → `CONTINUE`, `REPROLOGUE`, `AWAIT`, or `RAISE`
- `RETURN` — pop value → `RETURN(val)` or delimited plan arm
- `YIELD` — `YIELD(val)`

Unwind, parent delivery, boundary routers, and host stop are **not** opcode
concerns.

### 4.2 `apply_kos_op_result` — single apply function

**Problem today:** `cg_handle_op_result` and `eval_dispatch_op_result` are
duplicate switches. Category E expands the nested switch pattern.

**Combined-plan addition:** Introduce one static function in `eval.c`:

```c
/**
  Apply a KosOpResult from an opcode, native handler, dyn hook, or eval helper.
  Returns the terminal KosOpResult for koskari_eval, or an internal sentinel
  meaning "keep looping" (REPROLOGUE path uses longjmp/goto to cg_prologue).
*/
static KosOpResult apply_kos_op_result(KosEvalState *es, KosOpResult res);
```

Responsibilities (today spread across switches and `eval_event_*` callers):

| `res.kind` | Action |
|------------|--------|
| `NORMAL` | unreachable in apply (hot path only) |
| `REPROLOGUE` | goto `cg_prologue` |
| `FATAL` | return `EVAL_FATAL()` |
| `RAISE` | `r = eval_event_raise(es)` → apply `r` if not terminal |
| `YIELD` | `r = eval_event_yield(es, res.val)` → apply `r` |
| `AWAIT` | `r = eval_event_await(es, res.val)` → apply `r` |
| `RETURN` | `r = eval_event_return(es, res.val)` → apply `r` |
| `PARENT_RETURN` | `r = eval_parent_return_deliver(es, res.val)` → apply `r` |
| `CONTROL_RETURNED` | `r = kos_dynnode_on_control_returned(...)` or dedicated hook wrapper → apply `r` |

**Rule:** `eval_event_*` helpers return `KosOpResult` only. They **never** call
`goto cg_prologue` themselves; they return `REPROLOGUE` and let `apply` loop
once if needed (or apply handles one level of recursion inline to avoid stack
growth — implementation choice, behavior must match current).

**When:** Land during **Step 6** (Category E). Delete `eval_dispatch_op_result`
in Step 7 as elimination doc says.

### 4.3 Thin `RETURN` (boundary re-emit)

**Problem today:** `RETURN.c` still owns Phase 2a–2c (unwind, pop, parent
delivery, root stop). Same semantics as `eval_event_return` for part of the
space — two producers, one policy.

**Combined-plan addition — Step 6b** (after `eval_event_return` returns
`KosOpResult` without `out_ev`):

| `RETURN.c` keeps | Moves to `eval_event_return` |
|------------------|------------------------------|
| Pop operands, build value | `KOS_TRANS_WALK_RETURN_TO_FRAME` |
| Delimited path (`kos_return_delimited`) when prompts active | Async unwind / finally-taking-return staging |
| `EVAL_RETURN(val)` or plan arm | `kos_dyn_pop_frame`, `PARENT_RETURN` / root `RETURN` |
| | Iterator boundary via shared deliver helpers |

**Invariant (from boundary-control-reemit):** frame crossing and parent
consumer delivery are separate phases; both live under eval apply, not opcode.

**Exception:** Delimited 2a may remain opcode-local if it only arms a plan and
returns `REPROLOGUE` — document explicitly in `RETURN.c` comments.

### 4.4 Dyn boundary router (cap-driven)

After Track B, consolidate scattered `kos_dyn_need_kos_agen_boundary_deliver`
checks into one eval-owned router called from `eval_event_yield` /
`eval_parent_return_deliver` / `eval_event_return`:

```c
static KosOpResult kos_dyn_route_boundary(KosEvalState *es, KosOpResult incoming);
```

Walk caps on `KosEvalState_CurrentNode(es)` (NEXT, FOREACH, …); delegate to
Category B deliver helpers with `incoming.val`. Eval stays type-agnostic per
[generic-dynnode-protocol.md](generic-dynnode-protocol.md).

**When:** Step 6c, same PR as thin `RETURN` if tests allow; else immediately
after Step 9.

### 4.5 Optional: `KosPhase` (Track C)

Not required for elimination grep. After Tracks A+B stable, replace prologue
soup with:

```c
typedef enum KosPhase {
  KOS_PHASE_BYTECODE,
  KOS_PHASE_TRANSFER,
} KosPhase;
```

`REPROLOGUE` sets phase and re-enters top of loop. Pending PyErr becomes
`apply_kos_op_result(EVAL_RAISE())` at phase boundary. Defer unless prologue
remains hard to reason about after Step 6.

### 4.6 Entry / `KosEvalMode` (Track C)

`eval_phase_entry` + `KosEvalMode` overlap with `KosOpResult` (AWAIT/YIELD
stop vs REPROLOGUE). After elimination, consider having revive paths return
`KosOpResult` and only persist `KosSuspensionEvent` on context freeze. Lower
priority than Steps 1–9.

---

## 5. Unified implementation sequence

Single ordered checklist. **Do not skip tests between steps.**

### Track A — Elimination (from kosevent-elimination.md, with fixes)

| Step | Work | Notes |
|------|------|-------|
| **A1** | `kos_eval_prepare_boundary_raise`; retire `kos_eval_emit_raise_from_pyerr` | Elimination Step 1 |
| **A2** | Category B: deliver fns take `PyObject *val` | Elimination Step 2 |
| **A3** | Category D: wind/unwind/pop → `KosOpResult`; `try_local_exception_handler` → `KosOpResult` | Elimination Step 3 |
| **A3b** | **`kos_dynnode_on_control_returned` → `KosOpResult`** | **Fix §3.1**; not dead `out_ev` drop |
| **A4** | Category A: drop dead `out_ev` on install/helpers that truly ignore it | Skip `on_control_returned` (handled in A3b) |
| **A5** | Category C: install/call/push_or_await → `KosOpResult`; delete bridges and `KOS_OP_RES_*` | Elimination Steps 4–5 merged |
| **A6** | Category E + **`apply_kos_op_result`**; `eval_event_*` return `KosOpResult` | Elimination Step 6 + §4.2 |
| **A6b** | **Thin `RETURN.c`** → `eval_event_return` owns Phase 2b/2c | §4.3 |
| **A6c** | **`kos_dyn_route_boundary`** (optional same PR as A6b) | §4.4 |
| **A7** | `koskari_eval` → `KosOpResult`; no `out_ev` | Elimination Step 7 |
| **A8** | Drivers: `coro.c`, `iter.c`, `demand.c`, `YIELD_FROM.c` | Elimination Step 8 |
| **A9** | Delete `KosEvent` types; grep verification; remove `KosEvalResult` if unused | Elimination Step 9 + §3.5 |

### Track C — Optional polish (after A9 + tests green)

| Step | Work |
|------|------|
| **C1** | `KosPhase` explicit loop |
| **C2** | Collapse `KosEvalMode` / `KosEvalStep` into `KosOpResult` where possible |
| **C3** | Table-driven `apply_kos_op_result` for tracing/docs |

### Documentation updates (end of branch)

- Update [evaluator.md](evaluator.md) and [EVALUATOR.md](../EVALUATOR.md) §3
  (no `KosEvent`; `koskari_eval` returns `KosOpResult`).
- Mark [boundary-control-reemit.md](boundary-control-reemit.md) and
  [event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md)
  implementation status when A6b lands.
- On completion, move [kosevent-elimination.md](kosevent-elimination.md) to
  `design/archive/` with status banner (same pattern as eval-result-unification).

---

## 6. Verification

### 6.1 Elimination grep (unchanged)

```bash
grep -rn 'KosEvent\|KOS_EV_\b' koskari/lib/ --include='*.c' --include='*.h'
# must return nothing
```

Also grep tests and `koskari/types.py` if they reference `KosEvent`.

### 6.2 Tests (each step)

```bash
tox -qe test
```

After A6b, add or ensure coverage:

```bash
tox -qe py -- tests/lib/opcode/test_yield_from.py
tox -qe py -- tests/lib/test_demand_sync.py
tox -qe py -- tests/compile/specials/test_dyn_wind.py
tox -qe py -- tests/compile/specials/test_call_cc.py tests/lib/opcode/test_continuations.py
tox -qe py -- tests/reproducers/test_reset_shift_nested.py
```

### 6.3 Async-unwind → AWAIT matrix (Step A3)

Before merging Category D, verify rows still pass:

| Scenario | Expected observable |
|----------|-------------------|
| WIND after-thunk during RETURN unwind | Thunk runs; resume continues unwind; no spurious host AWAIT |
| WITH async `__exit__` during exception unwind | `try_local` or AWAIT; demand denial if in demand frame |
| Transfer plan step starts async wind | Plan stays armed; same `koskari_eval` loop |
| Nested `koskari_eval` from `YIELD_FROM` | Sub-driver switches on `KosOpResult.kind` |

If a row fails when `UNWIND_ASYNC_STARTED` becomes `KOS_RES_AWAIT`, use
`KOS_RES_REPROLOGUE` plus node phase instead for that path only.

### 6.4 Thin RETURN acceptance

- `grep -n 'KOS_TRANS_WALK_RETURN_TO_FRAME' koskari/lib/opcode/RETURN.c` —
  should be empty or only inside delimited helper called from eval
- Parent stack push / native handler delivery only in eval parent-delivery path

---

## 7. Encoding after combined work

Target vocabulary for agents (down from eight encodings in pre-unification
tree):

| Concept | Type | Role |
|---------|------|------|
| Opcode/dyn/eval effect | `KosOpResult` | **Only** in-loop signal |
| Host boundary | `KosOpResult` returned from `koskari_eval` | Same type, terminal kinds only |
| Resume tag on freeze | `KosSuspensionEvent` | AWAIT vs YIELD between drives |
| Transfer plan events | `KosXferEvent` | Unchanged; separate domain |

**Deleted:** `KosEvent`, `KosEventKind`, `KOS_EV_*`, `KOS_OP_RES_*` int protocol,
`kos_op_result_from_*` bridges, duplicate apply switches.

**Optional later deletion:** `KosEvalStep`, `KosEvalMode`, `KosEvalResult`.

---

## 8. Message to the elimination author

Your plan is the right **foundation**. Adopt these adjustments when merging:

1. **Upgrade `kos_dynnode_on_control_returned` to return `KosOpResult`** — do
   not list it under dead `out_ev` removal.
2. **Fix “What stays”** — `KOS_OP_RES_*` are removed in Step A5, not kept.
3. **Land `apply_kos_op_result` in Step 6** — avoid enshrining the large nested
   switch from Category E as the long-term shape.
4. **Schedule thin `RETURN` (Step A6b)** in the same effort — elimination alone
   still leaves the fattest semantic duplication in the tree.
5. **Treat `KOS_DYN_UNWIND_ASYNC_STARTED` → AWAIT** as behavior-sensitive;
   use the matrix in §6.3.

With those merges, one branch can deliver both **grep-clean elimination** and
**direct eval reasoning** without a second disruptive pass.

---

## 9. Non-goals (unchanged language features)

The combined plan does not simplify away:

- Transfer plans (multi-step unwind/install/wind/inject)
- Delimited continuations and nested plan suspend/replan
- Flattened async gen boundaries (`KOS_DYN_NEXT`, `KOS_DYN_FOREACH`)
- Demand denial (`eval_event_await`)
- VM-native wind/unwind thunks and async WITH phases

Complexity moves **up the type stack** (value-returned effects) and **into eval
apply**, not out of the language.

# The end.
