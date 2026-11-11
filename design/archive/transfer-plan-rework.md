# Transfer plan rework

Status: archived. Implemented on branch `transfer-plan-rework` (2026-09-09),
followed by the per-context recursion limit, the common-ancestor seam for
undelimited invokes, and the review fixes recorded in the branch history.
The permanent description is [EVALUATOR.md](../EVALUATOR.md) §6 and
[interpreter/extent-wind-unwind.md](../interpreter/extent-wind-unwind.md);
this is the design record as it stood when the work landed.
Line numbers are against that branch.

Transfer plans move control from the current dynamic chain to a target chain
state, running the extent protocol for the nodes that differ and delivering a
value or exception. Callers: continuation invoke (delimited and undelimited),
`POP_SHIFT` result delivery, foreign cc-unwind.

---

## 1. Defect ledger

All confirmed by reading, twice. D1 and D6 are also worth a runtime
reproducer; the rest are structural.

| id | where | defect |
|---|---|---|
| D1 | `xfer_plan.c:817`, `dynchain.c:1815-1823`, `dyn_with.c:458-471` | `kos_map_pop_result` `Py_XDECREF`s the AWAIT payload; drain returns NORMAL and re-runs the step; `EXIT_PENDING` then clears the triple and marks exit-done. A suspending `__aexit__` is never awaited and the in-flight exception is lost |
| D2 | `dynchain.h:787-788`, `eval.c:226-227` | `step_awaitable` never written; `step_resume_inject` never read. The documented step-suspension protocol does not exist |
| D3 | `xfer_plan.c:941-944`, `:965`, `:994`, `dyn_with.c:440` | `INSTALL`/`PUSH` suspend mid install-wind span and re-run whole. Second clone; `old_top` freed on the re-drain is the live executing `__enter__` frame; PUSH's remap sits after the early return so survivors keep template pointers; `current_top` re-read grafts clone two onto clone one |
| D4 | `eval_chain.c:292-297`, `dynchain.h:718-729` | replan reads `INJECT_VALUE` off whatever the last action is; `truncate_and_inject.node` shares offset 0 with `inject.value`. `Py_XINCREF` on a `KosDynNode *` |
| D5 | `xfer_plan.c:550`, `:556`, `eval_chain.c:328-333` | replan sets `unwind_from_current = 0` and gets `wind_count == 0` from `disjoint_ok`, discarding both halves of the plan it replaces; a NULL `original_target` abandons the outer transfer silently |
| D6 | `xfer_plan.c:816`, `:1177-1182`, `dynchain.c:1874-1876` | `EXTENT_ACTION` runs leave without unlinking; `TRUNCATE_AND_INJECT` then pops through `uninstall_handler`, running it again. Multi-shot `dyn-with` has no exit-done guard, so `shift` across one runs `__exit__` twice |
| D7 | `dyn_with.c:112-114` | `kos_dyn_with_clone` returns NULL with no `PyErr` for a pending node; surfaces as `SystemError: process_raise: no exception set` |
| D8 | `xfer_plan.c:997`, `:1049`, `:712`, `dynchain.c:3130` | `action_index--` on clone failure underflows to `SIZE_MAX`; free and traverse both iterate from `action_index`, so the INJECT value leaks and is GC-invisible |
| D9 | `eval_utils.c:278`, `eval.c:1471-1485` | plan suspension clears `KOS_CTX_RUNNING` and keeps running; `KosContext_Traverse` then reads the context fields the thaw NULLed. Chain and plan both go unvisited: missed collection |
| D10 | `xfer_plan.c:77-88` | `kos_transfer_plan_alloc` leaks the cached plan when `PyMem_Realloc` fails |
| D11 | `dyn_wind.c:110-112`, `:226` | `kos_dyn_wind_wind` guards only on `ENTERED`, set only when the thunk frame returns. No in-flight guard on the enter side, so a before-thunk that installs a frame re-fires on the next drain. No async required |

D11's trigger is a thunk containing a frame-installing call: threaded dispatch
tail-calls through ordinary opcodes, so only a boundary event
(`eval.c:3635` -> `goto prologue` -> `eval_step_drain_transfer_plan` at
`:3420`) re-enters the drain with the plan armed.

## 2. Inert or unnecessary code, deleted

- **LCA layer.** `graft_point_id` is the literal `0` at `dynchain.c:2649` and
  `:2768`; every target is a fresh clone whose nodes were never on the live
  chain, so the mark walk (`xfer_plan.c:34-45`) can never hit.
  `kos_dyn_find_lca` returns NULL on every call.
- `kos_dyn_plan_install_only` (`xfer_plan.c:642`): no callers.
- `KOS_DYNF_WALK_LIMIT`: set on GEN at `dyn_gen.c:16`, read nowhere.
- `KOS_XFER_POP_ONE` / `xfer_step_pop_one`: never emitted.
- **`KosDynCCRaise`** and `dyn_cc_raise.c`: the sentinel existed only so an
  action array could end in a raise. A goal has a `DELIVERING` phase (§3.8).
- **`kos_continuation_clone` on the invoke path** and the plan's
  `resume_stack` deep copy: the landed segment is itself a fresh clone of the
  template carrying the capture-time operands (`kos_dyn_clone_copy_segment`),
  so both copies replace identical content.

---

## 3. Design

### 3.1 Goal

```c
struct KosXferGoal {
  KosXferGoal *outer;          /* suspended enclosing goal */

  KosDynNode  *target;         /* owned clone of the template, or NULL */
  KosDynNode  *seam;           /* borrowed: leave down to this node; NULL = root */
  uint32_t     seam_id;
  KosDynNode  *frontier;       /* node whose thunk we are waiting on, or NULL */
  uint32_t     frontier_id;
  KosDynNode  *park_top;       /* dyn_top at the moment we parked */

  PyObject    *payload;        /* owned: value, or exception instance */
  PyObject    *continuation_ref;
  uint32_t     resume_pc;      /* UINT32_MAX = unset */

  uint8_t      phase;          /* LEAVING | LANDING | ENTERING | DELIVERING */
  uint8_t      is_throw;
  uint8_t      is_cc_unwind;
};
```

Fixed size. No action array, no index, no cache, no union.

Borrowed pointers are `seam`, `frontier`, `park_top`. Each is either owned by
this goal's own landing, or sits below everything the goal will unlink, so
it stays valid for the goal's lifetime; the two events that could free one
(a raise, an undelimited landing) free the goal first (§3.10). The ids are
assertions on resume, never lookups: node ids are not unique on a chain
(clones copy them, `dynchain.h:309`).

`target` is cloned at construction (§3.2 rule 3). No `resume_stack`, no
`invoke_clone` (§2).

GC: `kos_xfer_goal_traverse` visits `payload`, `continuation_ref` and the
`target` chain; `KosContext_Traverse` walks `goal` and every `outer` behind
it.

### 3.2 Driver

```
LEAVING     while dyn_top != seam:  unlink_top(ev = LEAVING)
LANDING     splice target onto the seam; set resume_pc on the landed frame
ENTERING    from the seam upward: enter(node) for each CONTROL_HOOK node
DELIVERING  push payload, or arm the throw (3.8)
```

Rules:

1. **Park.** When `enter()` or `leave()` answers REPROLOGUE or AWAIT:
   `frontier := node`, `park_top := dyn_top`, return that answer unchanged.
   The chain above `park_top` belongs to the thunk or callee; the driver
   never touches it.
2. **Resume.** On entry with `frontier` set: if `dyn_top != park_top`, return
   NORMAL. Otherwise `settle(frontier)`, clear `frontier`, continue.
   `dyn_top == park_top` is equivalent to "the thunk frame came and went":
   the thunk installs above `dyn_top`, so every boundary event while it runs
   has `dyn_top` at or above its frame, and the first event with `dyn_top ==
   park_top` is its return. For an AWAIT with no frame it holds at revive.
   For `LEAVING`, `park_top == frontier`; for `ENTERING`, `park_top` is the
   landed top and `frontier` a node inside the segment.
3. **`LANDING` is total.** Splice and pc install: no allocation, no
   suspension. The clone happens at construction, so a clone failure raises
   with the chain untouched. `LANDING` runs once; the phase byte records it.
   `LANDING` also restores the captured operands of the shared nodes from
   the seam down to its frame and retires every `outer` goal whose
   frontier `LEAVING` unlinked, all of them when the seam is NULL (§3.10).
4. **`LEAVING` and `ENTERING` are re-derived.** A node that has left is off
   the chain; a node that has entered has `ENTERED`. `settle` and `abort`
   advance node state only; nothing advances the goal but the driver.
5. **A raise frees the goal stack.** `process_raise` keeps doing what it does
   at `eval.c:1049` (`kos_abandon_transfer_nest`, cc-apply excepted). The
   chain a walk left behind is a coherent chain to raise through. The
   frontier is therefore never removed from under a live goal; on resume,
   `frontier_id` on the chain is an assertion.

The drain site is unchanged: `eval_step_drain_transfer_plan` at
`eval.c:3420`, in the prologue, entered on every boundary event.

### 3.3 Extent contract

Four entry points, gated by `KOS_DYN_CAP_CONTROL_HOOK` (exactly WIND union
WITH):

```c
KosOpResult kos_dyn_extent_enter(KosEvalState *es, KosDynNode *node);
KosOpResult kos_dyn_extent_leave(KosEvalState *es, KosDynNode *node,
                                 KosXferEvent ev);
int         kos_dyn_extent_settle(KosEvalState *es, KosDynNode *node);
void        kos_dyn_extent_abort(KosDynNode *node);
```

`enter` / `leave` answers:

| answer | meaning | caller |
|---|---|---|
| NORMAL | obligation discharged | proceed; `leave` -> unlink |
| REPROLOGUE | a frame was installed | park, return |
| AWAIT(val) | suspended on an awaitable | park, return with payload |
| RAISE | the node's own code raised | `process_raise` |

`settle`: the thunk or awaitable finished; take its result from the live
block, advance `PRE_* -> POST_*`. `abort`: the thunk died; advance without a
result. A `PRE_*` phase means in flight and nothing else. Who calls what:

| site | when | does |
|---|---|---|
| post-return hook (`kos_eval_post_opcode_control_returned_after_hook`) | `dyn_top` is a `CONTROL_HOOK` node in a `PRE_*` phase | `settle(dyn_top)`; then, unless a goal's `frontier == dyn_top`, `unlink_top(ev = the obligation's event)` when the settled phase is a leave |
| await revive (`kos_dyn_complete_pending`) | same condition, awaited value already on the block | same |
| goal driver | rule 2 | `settle(frontier)` |
| any walk | `leave(node, ev)` finds `PRE_*` at `dyn_top` | `abort(node)` inside `leave` |

An in-flight node's thunk runs in a frame above it. `leave()` on an
in-flight node that is `dyn_top` therefore finds that frame gone -- it
raised, or the walk crossed it -- and aborts, whatever `ev` is; on one that
is not `dyn_top` it answers REPROLOGUE. An undelimited invoke from inside an
after-thunk crosses that thunk's own wind this way: the thunk is abandoned,
never waited for.

Rules: asking again is always legal; the node never unlinks itself; the node
never reads the goal.

### 3.4 Per-kind state machines

WIND phases unchanged: `IDLE`, `PRE_BEGIN`, `POST_BEGIN`, `PRE_AFTER`,
`POST_AFTER`. WITH gains a terminal `LEFT` beside `IDLE`,
`PENDING_ENTER_AWAIT`, `PENDING_EXIT_AWAIT`; today it returns to `IDLE` after
a sync exit (`dyn_with.c:355`), so a re-ask re-fires `__exit__`.

**`enter(WIND)`**

| state | action | answer |
|---|---|---|
| `ENTERED` | none | NORMAL |
| `PRE_BEGIN` | none, thunk in flight | REPROLOGUE |
| no before-thunk | `ENTERED := 1` | NORMAL |
| otherwise | fire before-thunk, `PRE_BEGIN` | REPROLOGUE / AWAIT |

**`leave(WIND, ev)`**

| state | action | answer |
|---|---|---|
| `POST_AFTER` | none | NORMAL |
| `PRE_*`, node is `dyn_top` | `abort`; the thunk frame is gone | NORMAL |
| `PRE_*` | none, thunk in flight above | REPROLOGUE |
| not `ENTERED`, or no after-thunk | `POST_AFTER`; `body_result` per §3.6 | NORMAL |
| otherwise | fire after-thunk, `PRE_AFTER` | REPROLOGUE / AWAIT |

`settle(WIND)`: `PRE_BEGIN` -> pop the thunk result, `ENTERED := 1`,
`POST_BEGIN`. `PRE_AFTER` -> pop the thunk result, `POST_AFTER`.
`abort(WIND)`: `PRE_*` -> `POST_AFTER`.

**`enter(WITH)`**

| state | action | answer |
|---|---|---|
| `LEFT`, single-shot exit-done, or `ENTERED` | none | NORMAL |
| `PENDING_ENTER_AWAIT` | none, `__enter__` in flight | REPROLOGUE |
| otherwise | fire `__enter__`; completed -> bind, `ENTERED := 1`; else `PENDING_ENTER_AWAIT` | NORMAL / REPROLOGUE / AWAIT |

**`leave(WITH, ev)`**

| state | action | answer |
|---|---|---|
| `LEFT`, or single-shot exit-done | none | NORMAL |
| `PENDING_*`, node is `dyn_top` | `abort`; the call's frame is gone | NORMAL |
| `PENDING_*` | none, call in flight above | REPROLOGUE |
| not `ENTERED` | `LEFT`; no `__exit__` | NORMAL |
| otherwise | fire `__exit__` with the triple (`ev == RAISE`) or None x3; completed -> `LEFT`; else `PENDING_EXIT_AWAIT` | NORMAL / REPROLOGUE-on-swallow / REPROLOGUE / AWAIT |

`settle(WITH)`: `PENDING_ENTER_AWAIT` -> pop, bind, `ENTERED := 1`, `IDLE`.
`PENDING_EXIT_AWAIT` -> pop `__exit__`'s result, `LEFT`; a truthy result
against a parked triple discards the obligation and sets `resume_pc`.
`abort(WITH)`: `PENDING_*` -> `LEFT`.

`kos_dyn_wind_on_control_returned`, `kos_dyn_with_on_control_returned`,
`kos_dyn_with_on_enter_control_returned`, `kos_dyn_dyn_with_wind` and
`kos_dyn_with_unwind` are reshaped into these; their plan-index advances
(`dyn_wind.c:225-241`, `:270-296`), self-pops, and the `Py_XDECREF(wr.val)`
at `dyn_wind.c:117` and `:180` go.

PARAM keeps its synchronous wind/unwind and is reached through §3.5.

### 3.5 Unlink primitive

```c
KosOpResult kos_dyn_unlink_top(KosEvalState *es, KosXferEvent ev,
                               int allow_barrier);
```

1. `CONTROL_HOOK`: `r = leave(node, ev)`; return `r` unchanged unless NORMAL.
2. Synchronous per-kind teardown (`kos_dyn_uninstall_handler`'s PARAM,
   YIELD_FROM, TRY, FINALLY arms).
3. Detach the node's obligation (§3.6) into locals.
4. Commit the parent as `dyn_top`, clear the parent link, free the node.
5. Act on the detached obligation per the §3.6 table; return that result,
   NORMAL when there was none.

Step 5 after step 4: releasing a RETURN re-enters the return path, which
unlinks; the node must already be gone.

Barrier policy is the argument; `kos_dyn_pop_nonbarrier_top`,
`kos_dyn_pop_top_allow_barrier` and `kos_dyn_pop_top` collapse into it. The
per-kind fast pops (`kos_dyn_pop_wind`, `kos_dyn_pop_with`,
`kos_dyn_pop_try`, ...) become wrappers that assert kind and call the
primitive. `PUSH_WITH.c:74` and `PUSH_DYN_WITH.c:79` pop after a failed
`__enter__` and are correct because §3.4's not-`ENTERED` row answers NORMAL
without firing `__exit__`. That row is load-bearing.

### 3.6 Obligations

A suspended obligation is a value with nowhere to sit because the walk
carrying it stopped at a node whose leave went asynchronous. Storage: inline
in the WIND and WITH union arms, replacing fields those arms already spend on
the same job (WITH's `exc_typ`/`exc_val`/`exc_tb`/`normal_exit`; WIND's
`body_result`).

```c
uint8_t ob_kind;              /* NONE | VALUE | EXCEPTION | RETURN */
union {
  PyObject *value;                                   /* VALUE */
  KosExcState exc;                                   /* EXCEPTION */
  struct { PyObject *value; uint8_t path; KosDynDelim *delim; } ret;  /* RETURN */
} ob;
```

`VALUE` is a wind's body result, parked by `POP_WIND` for the unlink to
deliver; it is a kind rather than a separate field so the release table
below is the whole story.

`RETURN.path` is the return kind (bytecode frame, native frame, delimited
route); `delim` is the bookmark for the delimited route, borrowed, below the
node, alive for the obligation's lifetime by the same argument as §3.1.

WIND union WITH is the complete parking set: `dynchain.c:1900-1975` shows
those are the only kinds whose enter or leave answers non-NORMAL without
erroring, and `KOS_DYN_CAP_CONTROL_HOOK` already names exactly that set.
FINALLY holds a pending return in its own `handling_result` with its own
`POP_FINALLY` logic and is untouched.

One slot per node. A parked exception and a parked return at the same node
are mutually exclusive (a raise inside an after-thunk supersedes an in-flight
return); an occupied slot is an assertion.

**Release**, at §3.5 step 5, by obligation kind and the event that unlinked
the node:

| obligation | `RAISE` (thunk raised) | `NONE` (thunk finished) | `TRANSFER` (crossed by a goal) |
|---|---|---|---|
| RETURN | discard | continue the return via `path` | discard |
| EXCEPTION | resume: with another exception in flight, chain as its `__context__`; else re-raise | resume | discard |
| VALUE | discard | push onto the revealed block | discard |

`RAISE` and `NONE` share the EXCEPTION rule because a with's `leave` parks
the in-flight exception itself before firing `__exit__`: when `__exit__`
returns falsy synchronously the unlink runs under `RAISE` with `PyErr`
clear and the parked exception is the one propagating.

A RETURN under `NONE` is handed back to the caller of the unlink rather
than acted on inside it: the return paths live in the evaluator, and
`kos_dyn_settle_top` is the only caller that can receive one. Its two
sites (the post-return hook and the await revive, which passes it up to
the entry phase) resume it through `KosEval_ResumeReturn`.

`RETURN` or `EXCEPTION` under `ev == RETURN` is unreachable: a walk from
inside a thunk stops at the thunk frame. Assert.

The walks own parking: a walk that gets REPROLOGUE or AWAIT back from
`unlink_top` writes its payload into `dyn_top`'s slot and returns the answer.
The raise walk fetches `PyErr` before popping a wind that is about to fire
its after-thunk, so the thunk runs clean, and parks it on the wind once the
thunk has started; a wind or with whose call is already dead (in flight at
`dyn_top`) is aborted by the unlink and never fires, so the exception stays
pending for step 5 to chain what the node held. A leave that raises
synchronously leaves its node in place; the raise walk unlinks it again
under `RAISE`, which aborts it and chains. The driver parks nothing on
nodes; its payload lives in the goal.

Sizing: `with` is 88 bytes, tying `yield_from` and `gen` for the widest arm.
The union above is 24 bytes plus the kind byte, replacing 28; `wind` at 32
has the slack outright. Node size does not change.

Rejected: a heap `KosObligation` behind a base-node pointer (an allocator, a
freelist, a NULL check on every node, and a second lifetime, for 32 bytes
already present). Rejected: a context-level stack keyed by node id (the raise
walk must then scan it for entries owned by nodes it pops, which is an
invariant kept by remembering to check it). Node-carried storage is safe
because §3.5 is the only free site.

Clone-or-refuse, at `kos_dyn_clone`, with a `PyErr` on every refusal (D7):

| kind | state | clone answer |
|---|---|---|
| WIND | any obligation, or `PRE_*` | refuse |
| WITH | `PENDING_*` | refuse (as today, plus the message) |

Deleted: `KosContext.pending_return_*`, `kos_ctx_exc_stash_*`,
`kos_dyn_chain_has_node_id`, `kos_dyn_reaches` in its guard role. This
subsumes [pending-return-across-suspension.md](pending-return-across-suspension.md):
its defects 3, 4, 7 and 8 stop existing. Its defect 2 (`DISPATCH` unmapped
in the entry phase) closed when the entry phase's resume block was rewritten
around `KosEval_ResumeReturn`. Its defect 1 (GC over-attribution:
`kos_dyn_prepare_push` gives every non-frame node an owned reference to the
live namespace, and the untracked-namespace report ran once per node and
walked parents) closed by reporting an untracked namespace only from the
node that introduced it -- a frame by kind, since a parked frame's caps are
cleared, or a `KOS_DYN_CAP_BINDS_NS` node -- and one level only.

### 3.7 Seam and target per caller

| operation | target | seam |
|---|---|---|
| undelimited call/cc invoke | clone of the template above the common ancestor's counterpart | the lowest common ancestor of the live chain and the template by node id; NULL when they share no node |
| delimited push invoke | clone of the captured template | the DELIM this invoke pushed |
| `POP_SHIFT` | NULL | the prompt's parent |
| cc-unwind | NULL | the root GEN if the context has one, else NULL |

The delimited segment grafts onto the DELIM (`xfer_plan.c:1057` today), so
the DELIM is the seam, not the node below it; it is created per invoke and
never cloned, so it is unique.

### 3.8 Delivery

`DELIVERING` pushes `payload` onto the live block, or arms
`KosContext_INJECT_*` for a throw. Always a push: a template carries no live
resume (`kos_dynframe_clone` refuses step resumes by name; the only node
resume is yield-from, `dyn_yield_from.c:333`, which needs a GEN, which
capture refuses). Assert `RESUME_FN == NULL` on the landed frame.

cc-unwind: `DELIVERING` with `is_cc_unwind` does
`kos_eval_prepare_boundary_raise` and returns RAISE, bypassing
`process_raise` so no local handler swallows another context's signal. This
replaces the `KosDynCCRaise` sentinel and `xfer_plan_step_is_cc_raise_reemit`.

A native-frame invoker of a delimited continuation is served by
`kos_dyn_finish_delimited_pop_return` (`dynchain.c:3191-3205`), unchanged.

### 3.9 Leave semantics per kind

`LEAVING` runs §3.5 on every node down to the seam. Today the plan runs
extent actions for qualifying nodes and lets `kos_dynchain_free` discard the
rest with no teardown, so TRY/FINALLY exception-state restore and YIELD_FROM
unwind are skipped; running them is a correctness gain.

| node at `dyn_top` | `LEAVING` |
|---|---|
| WIND, WITH | §3.3, `ev = LEAVING`; `body_result` discarded |
| PARAM, YIELD_FROM, TRY, FINALLY, NS, COLLECT, LOOP, FOREACH | synchronous teardown, unlink |
| PROMPT, DELIM, SHIFT, CC, NEXT | unlink; `allow_barrier = 1` for every goal |
| GEN, flattened (has a parent) | the raise walk's arm: `ki_finish`, teardown as exhausted, continue |
| GEN, root (no parent) | the seam for cc-unwind (§3.7); unreachable for any other goal, since capture already refuses across a GEN. `SystemError` |

### 3.10 Nesting

`goal->outer` is a stack. An inner goal starts from inside a thunk while the
outer is parked (rule 1); the outer resumes when its own rule 2 condition
holds, which for a delimited inner is after the inner's slice has routed back
through the inner DELIM and the thunk returned. A delimited goal is finished
at `DELIVERING`; the slice's completion is the DELIM bookmark's business.

An inner `LANDING` frees every outer whose frontier its `LEAVING`
unlinked (pointer reach and id, as the seam check): the node the outer
waited on and everything above it are gone, so the extent it was moving
through no longer exists. An undelimited inner whose seam is the common
ancestor frees the outers parked above that ancestor and leaves one parked
at or below it; a NULL seam (no shared node) frees them all. A delimited
inner never unlinks below its own DELIM and cannot invalidate an outer. A
raise frees the stack (rule 5).

Deleted: the replan block (`eval_chain.c:270-350`),
`kos_dyn_plan_direct_install`, `kos_xfer_plan_is_delimited_push`, both
"nested continuation invoke during an active delimited transfer" refusals,
`original_target`, `graft_point_id`.

---

## 4. Sub-assembly verdicts

| sub-assembly | verdict | notes |
|---|---|---|
| Result currency | **REPLACE** | `KosOpResult` throughout. Delete `kos_map_pop_result`, `KOS_DYN_UNWIND_ASYNC_STARTED`, `KOS_XFER_SUSPENDED_AWAIT`, `KosWalkStatus`, `kos_run_transfer_step`'s private codes. `FINALLY_TAKING_RETURN` and `DELIM_TAKING_RETURN` are immediate deliveries with a PC change and stay as caller-handled results; only `ASYNC_UNWIND` becomes an obligation |
| Extent contract | **CHANGE** | §3.3, §3.4 |
| Per-kind state machines | **CHANGE** | §3.4 |
| Unlink site | **CHANGE** | §3.5 |
| Obligations | **REPLACE** | §3.6 |
| Goal representation | **REPLACE** | §3.1 |
| Goal construction | **REPLACE** | §3.7. Delete six builders, `KosXferPlanOptions`, `kos_build_xfer_plan`, the count/fill helpers, the LCA layer, `KosDynCCRaise` |
| Goal driver | **REPLACE** | §3.2. Delete `kos_run_transfer_step`, the six `xfer_step_*`, `xfer_step_land_segment`, `xfer_remap_extent_nodes`, `kos_dyn_run_install_wind_span` |
| Goal nesting | **CHANGE** | §3.10 |
| Delivery | **CHANGE** | §3.8 |
| The walks | **CHANGE** | `kos_dyn_unwind_to_node` / `_to_frame` / `_return` / `_to_loop` return `KosOpResult`, pop through §3.5, park per §3.6. `kos_dyn_unwind_raise` changes least: pops through §3.5, its `saved_exc_*` block becomes an obligation write, its GEN arms are the ones §3.9 reuses |
| Capture, clone | **KEEP** | plus §3.6's clone answers. `kos_invoke_continuation` builds a goal with a construction-time clone and no longer calls `kos_continuation_clone` |
| In-place invoke | **KEEP** | the refusal at `xfer_plan.c:1413-1414` restated as "no goal armed" |

The goal lives in its own translation unit, `koskari/lib/xfer_goal.c` with
`xfer_goal.h`: build a goal, arm it, drive it, free it, traverse it. Nothing
in the driver switches on `KosDynNode_KIND` beyond the GEN and PARAM rows of
§3.9 and §3.2. `xfer_plan.c` keeps only continuation invocation (the in-place
path and `kos_invoke_continuation`, which builds a goal) and stays a textual
include of `dynchain.c`, since the in-place path uses its statics.

## 5. Order of work

1. §3.3 through §3.5: `settle`/`abort`, the state machines, the unlink
   primitive. Closes D6, D7, D11. Not behavior-preserving: WITH's `LEFT`
   phase and the not-`ENTERED` row change what runs on existing pop paths,
   so this step carries its own tests. Bridge for the old driver, deleted
   in step 4: `park_node` and `park_top` on `KosTransferPlan`; an extent
   step resumes its own node once `dyn_top == park_top` (rule 2 on the old
   plan) and skips a node in flight for someone else, whose thunk the
   INSTALL replaces the chain under.
2. Result currency. Closes D1 and, by making AWAIT reach the driver, D9;
   verify D9 with the GC test in §6. Bridge for the old driver, deleted in
   step 4: an extent step's AWAIT payload rides in `step_awaitable` to the
   drain, which hands it to the driver through `eval_event_await`; the
   entry phase routes an AWAIT-suspended plan through the await revive, so
   the settled value lands on the block for the step's `settle`. The
   walks consume what they carry on every answer but NORMAL, and a leave
   that raises during a return walk now reaches `process_raise` where it
   was EVAL_FATAL.
3. §3.6.
4. §3.1, §3.2, §3.7, §3.8 as one change, with §3.9 and §3.10, since the
   driver cannot exist without its nesting and its GEN rule. Closes D2, D3,
   D4, D5, D8, D10. Two test expectations change with it: an unbounded
   nested delimited invoke from a before-thunk, which the removed refusal
   used to stop, becomes the bounded shape (a recursion limit is the
   follow-up that reinstates the unbounded program under
   `RecursionError`); and an undelimited invoke from an after-thunk during
   another undelimited transfer now yields the inner continuation's value,
   the outer transfer being void per §3.10, where the replan used to
   deliver the outer's.
5. Remaining: the goal-side review items, ASan, pyversions.

## 6. Tests this must add

Named as required in `with-dyn-with.md` and absent today:

- dyn-with with a genuinely suspending `__aenter__` / `__aexit__` crossed by a
  goal, once per phase: `LEAVING`, `ENTERING` (D1, D3).
- `shift` crossing a multi-shot `dyn-with` (D6).
- `dynamic-wind` whose before-thunk installs a frame, crossed by a call/cc
  invoke, with the wind both at the template's top and below a frame (D11,
  rule 2).
- a wind thunk that invokes a continuation, delimited and undelimited, from
  `LEAVING` and from `ENTERING` (D4, D5, §3.10).
- a wind thunk that raises, during a goal and during a plain return (rule 5,
  §3.6 table).
- a wind thunk that calls a Koskari function which captures a continuation.
- a wind thunk that drives a Koskari generator which awaits.
- cc-unwind delivered into a generator context (§3.7, §3.9).
- BREAK/CONTINUE through a WITH or WIND with an async exit.
- an abandoned context mid-transfer, with `gc.collect` and a weakref (D9).

## 7. Supersedes

The closing paragraph of [extent-wind-unwind.md](../interpreter/extent-wind-unwind.md)
("Suspend during wind/unwind") describes the `step_awaitable` /
`step_resume_inject` protocol, which is D2. Replaced by §3.2 and §3.3; its
per-kind table is replaced by §3.4.

Related: [callcc.md](callcc.md),
[extent-wind-unwind.md](../interpreter/extent-wind-unwind.md),
[with-dyn-with.md](../interpreter/with-dyn-with.md),
[pending-return-across-suspension.md](pending-return-across-suspension.md),
[archive/interrupted-transfer-plans.md](../archive/interrupted-transfer-plans.md).
