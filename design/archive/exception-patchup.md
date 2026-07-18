# Exception patch-up — cap gating, unified stash, unwind propagation

## Status

**Stages 1-4 implemented; two decisions remain.**

- **Stage 1 (§3)** — failed-pop taxonomy landed: FATAL passthrough,
  barrier refusals classed as machinery noise (restore + recovery), user
  teardown failures supersede with `__context__` chaining and the walk
  continues (discard-on-stuck guarantees progress).
- **Stage 2 (§2)** — context-owned LIFO exception stash
  (`KosContext.exc_stash`, single normalized instances tagged with the
  parking node_id) with the helper family in dynchain.c; WIND's
  `pending_exc_*` fields deleted.
- **Stage 3 (§1)** — `KOS_DYN_CAP_INTERCEPTS_RAISE` gates the raise walk
  dispatch (TRY, FINALLY, GEN, INPLACE DELIM); kept distinct from
  `HANDLES_EXCEPTIONS` (failed-pop recovery; PROMPT proves the sets
  differ).
- **Stage 4 (§4)** — crossed frames recorded into
  `__koskari_dynchain__` (marked `"crossed"`) before the pop;
  escape-time decoration carries them forward without duplication.
- **Stage 5 (§5)** — the suspension gap was real (two contexts suspended
  inside handlers on one thread corrupted each other's exc_info).
  **Decided and implemented**: handler-visible exception state is
  context-owned; `koskari_eval` swaps the thread's exc_info with the
  context's saved triple at entry and reverses at cleanup (all-empty
  fast path skips the swap). Contexts are isolated — a fresh context
  starts with empty handler state, no fall-through to the host's
  (upgradeable to CPython-style linked exc_state items via the compat
  headers if fall-through is ever wanted).
  `test_exc_info_isolation.py` guards both the single-task and the
  interleaved cases.
- **Stage 6 (§6)** — **decided and implemented: node-authoritative.**
  The extent table's job ends at push time (it supplies the jump
  positions stored on the node); no runtime extent consult remains on
  the raise path. Removed: the frame-boundary extent check
  (`try_local_extent_on_current_frame`), the handler-pc merge in
  `kos_dynnode_raise_handler_pc_for_try_finally`, the extent lookup in
  `kos_dyn_handle`'s TRY case, and the now-dead `kos_try_handler_for` /
  `KosTryHandler`. Evidence: full suite green without any of them.

**Last updated:** 2026-07-02

## Audience

Maintainers working on the raise paths: `eval.c` (`process_raise`),
`eval/eval_chain.c`, `dynchain/dyn_walk.c`
(`kos_dyn_try_local_raise_body`), and the per-kind units in `dynchain/`.

## Related documents

| Document | Relationship |
|----------|----------------|
| [callcc.md](../interpreter/callcc.md) | Delimited continuation machinery the raise paths must respect |
| [delimited-invoke-model.md](delimited-invoke-model.md) | Two-path invoke; the INPLACE DELIM restitch is folded into this design |
| [boundary-control-reemit.md](boundary-control-reemit.md) | Capacity-driven dispatch precedent (§5.2) |
| [../TODO.md](../TODO.md) | Single rollup entry pointing here |
| `tests/compile/specials/test_raise_across_frames.py` | Frame-crossing propagation regressions |
| `tests/compile/specials/test_reset_shift.py` | Slice exception routing regressions |
| `tests/compile/specials/test_dyn_wind.py` | After-thunk-on-raise regression |

---

## The model

Dynamic nodes are semi-generic. Each node's **caps bitfield is its API
descriptor**: it declares which generic methods the node implements —
install, uninstall/unwind, control-returned, exception handling. Dispatch
of those generic methods is via static switch functions rather than
vtables (an implementation conceit: all node kinds are owned by this
codebase). The reasoning is meant to be straightforward and almost
object-like.

For exceptions, the raise walk is **handle-or-remove**:

1. **Cap fast check first.** One bitmask test answers "does this node
   participate in exception handling?" before any call is made.
2. **Handle** (cap-positive): dispatch to the kind-specific handler via
   `kos_dyn_handle`. The handler may perform work (except, finally, with,
   dyn-with, dyn-wind) and may **swallow** the exception, clearing it.
   Handling nodes own the stash they need to resume.
3. **Remove** (cap-negative): unwind or pop. Uninstall checks the unwind
   cap and runs teardown, else just pops. An unwind running due to an
   exception may stash likewise. Unwind/pop may **suspend via await, but
   never exits the evaluator loop with the exception condition.**

A raise leaves the evaluator loop only at the chain root (NULL parent) or
a generator boundary. Frames are crossed, not terminal: an unhandled
raise checks the frame's try extents, records the frame in the trace, and
continues below it.

### Failed-pop taxonomy

Two distinct failure classes during removal:

1. **Evaluator-internal fault** — Koskari itself broke (bad chain
   topology, invariant violation). Returns `KOS_RES_FATAL`; allowed to
   abend the loop. A Koskari bug, never a user-code condition.
2. **User unwind failure** — an after-thunk, `__exit__`, or restore
   raised. Never abends. It becomes a **new exception chained from the
   original via Python `__context__`**, and the walk continues with the
   new exception from the current chain position. Nothing is lossily
   dropped.

### Storage split

- **Propagation state is context-owned.** The exception in flight belongs
  to the walk, and the walk's persistent home is the context — same as
  `pending_return_value` and the `inject_*` pending-throw fields already
  are. Node-resident parking dies with its node, and this system
  performs wholesale chain surgery (INSTALL frees old chains, graft
  teardown discards clones, transfer nests abandon); an in-flight
  exception must survive all of it.
- **Handler state is node-owned.** What a node needs to do its own job —
  WITH's `normal_exit` and `__exit__` argument sourcing, FINALLY's
  `HANDLING_RAISE`, DELIM's `saved_stack`/`saved_pc` resume fields — stays
  in the node payload, per the object model.
- **exc_info is the bytecode contract only.** `EXCEPTION` /
  `CHECK_EXCEPTION` read it; TRY/FINALLY handler staging writes it. It is
  thread state and the evaluator does no per-context swapping, so it is
  never a cross-suspension or cross-context stash.

---

## Changes

### 1. Cap-gated dispatch

- The raise walk consults `KOS_DYN_CAP_HANDLES_EXCEPTIONS` (today read
  only on the failed-pop recovery branch) **before** calling
  `kos_dyn_handle`. Cap-negative nodes go straight to unwind/pop; the hot
  loop stops calling the kind-switch just to receive PASS.
- Set the cap on every kind that handles RAISE: TRY, FINALLY, GEN. The
  INPLACE DELIM restitch (currently a kind+mode special case inlined in
  the walk) and the WITH swallow ride the same gate and dispatch.

### 2. Context-owned exception stash stack

- `KosContext` gains a **LIFO stash stack** of parked exceptions. Parks
  nest strictly LIFO (an outer unwind is frozen until the inner
  completes), so a stack models overlapping unwinds exactly.
- Each entry is a **single normalized exception instance** (`PyObject *`;
  the traceback rides `__traceback__`), tagged with the parking node's
  `node_id` for assertions. Not a type/value/tb triple.
- Helper trio: **stash** (fetch + normalize + push), **reraise** (pop +
  restore into PyErr, asserting the node_id tag), **supersede**
  (`PyException_SetContext(new, old)` when a teardown raises over a
  park).
- Cleanup rendezvous: `kos_abandon_transfer_nest` and context teardown
  pop and drop stale entries **loudly** (debug assertion / trace) — a
  dropped in-flight exception must never be silent.
- Migrations, each independently green:
  - WIND `pending_exc_type/value/tb` fields → stack (delete the fields;
    `kos_dyn_wind_on_control_returned` re-raises via the helper).
  - CC_RAISE payload → evaluate riding the stack.
  - WITH keeps `normal_exit` but sources `__exit__` arguments from the
    park instead of private `exc_typ/val/tb` copies.
  - DELIM `saved_*` fields stay — resume state, not exception parking.

### 3. Unwind propagation and chaining

- Return-code discipline at every removal site: internal-invariant
  failures return FATAL; anything that ran user code returns RAISE.
- The walk's failed-pop branch becomes a two-way switch: FATAL abends;
  RAISE supersedes (chain the parked original as `__context__` of the
  new exception) and continues the walk.
- Fixes two current lossy paths: the sync teardown failure restores the
  parked original **over** the teardown's own exception (dropping it);
  the async supersede drops the parked original unchained.
- `kos_eval_prepare_boundary_raise` / `kos_eval_mark_context_dead` become
  exclusive to true loop exits (chain root, GEN boundary) and FATAL.

### 4. Per-frame traceback collection

- Crossing a frame during the raise walk records that frame into the
  exception's trace before the frame is popped. Today
  `decorate_exception_with_dynchain` snapshots the live chain only at
  escape time, so crossed frames vanish from `__koskari_dynchain__`.
- Requires an append-style decoration (create-or-extend) instead of the
  current overwrite-at-escape.

### 5. exc_info scoping and the suspension gap

- Keep exc_info exclusively for handler-visible state (change nothing in
  the bytecode contract).
- **Latent bug, needs a reproducer**: an await suspension inside an
  except/finally handler hands the thread back to the event loop with
  exc_info unswapped — other tasks and other koskari contexts share and
  can clobber it (CPython swaps per-generator `exc_state` for exactly
  this reason). Decide between per-context exc_info swap at freeze/thaw
  or documenting the restriction; measure the freeze/thaw cost first.

### 6. TRY source of truth (decision needed)

The TRY node carries the dynamic one-shot latch (`ENABLED`); the static
code extent table (`try_local_extent_on_current_frame`,
`kos_try_handler_for`) is a parallel node-less discovery path at frame
boundaries. The handler-range exclusion in `kos_try_handler_for` narrowed
their disagreement (the extent-refire loop), but the split remains.
Decide: extent-table authoritative (nodes carry only dynamic state) or
node-authoritative (extent check retired to a transitional shim). The
cap-gating work will force the question.

---

## Suggested staging

| Stage | Work | Proof |
|-------|------|-------|
| 1 | Taxonomy discipline + supersede chaining (§3) | Teardown-raises tests, sync and async, asserting `__context__` |
| 2 | Context stash stack + WIND migration (§2) | Existing wind/with/slice exception tests stay green; field deletion compiles |
| 3 | Cap gating + fold walk special cases (§1) | Full suite; no behavior change expected |
| 4 | Per-frame trace collection (§4) | Traceback content assertions across crossed frames |
| 5 | exc_info suspension reproducer + decision (§5) | New red reproducer per test policy |
| 6 | TRY source-of-truth decision (§6) | Follows from 3 |

Defect reproducers follow the standing policy: assert correct behavior,
red until fixed, never pinned to a defect.

---

## Already landed (this branch)

Context for the reader; these are the localized fixes the design
supersedes or absorbs:

- Frame-crossing raise propagation, tail-call pop guards, and the
  handler-range exclusion in `kos_try_handler_for`.
- WIND `pending_exc_*` parking (absorbed by §2) and the raise-walk
  restitch for INPLACE DELIMs (absorbed by §1).
- WITH single-shot exit-done sharing via a KosRef cell (stays; it is
  handler state, not propagation state).
