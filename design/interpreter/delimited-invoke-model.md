# Delimited continuation invoke — target model

## Status

**Design target — not implemented.** Documents how delimited `k(value)` invoke
should behave and how it differs from the current PUSH-only path in
`kos_invoke_continuation`. Intended to guide a future refactor of
`kos_shift_capture`, delimited invoke, and `POP_PROMPT` return routing.

**Last updated:** 2026-06-10

## Audience

Maintainers and agents working on `reset`/`shift`, dyn-wind + delimited invoke,
and transfer-plan policy.

## Related documents

| Document | Relationship |
|----------|----------------|
| [callcc.md](callcc.md) | Current implementation (PUSH model, DELIM markers) |
| [interrupted-transfer-plans.md](interrupted-transfer-plans.md) | Transfer-plan follow-ups, heritage capture, §6.6 forbid |
| [extent-wind-unwind.md](extent-wind-unwind.md) | Wind/unwind thunk execution |
| [../TODO.md](../TODO.md) | Open work rollup |
| `tests/compile/specials/test_reset_shift.py` | Capture + invoke regressions (in-shift vs remote) |

---

## Current implementation (summary)

Delimited invoke today (`kos_invoke_continuation`, delimited branch):

1. **Token check** — `token_ref` must match current evaluator (in-context gate).
2. **`kos_dyn_push_delim`** — cheap return bookmark: invoke-site `RETURN_FRAME`,
   `RETURN_PC`, `RETURN_NODE` (not a `KosContinuation`; `RETURN_CHAIN` is NULL).
3. **`kos_build_delimited_push_plan`** — `PUSH` clones the captured template,
   grafts it onto `dyn_top` (above DELIM), then wind `EXTENT_ACTION`s, then
   `INJECT`.
4. **Completion** — template runs until `POP_PROMPT`; `kos_find_nearest_delim`
   unwinds to the bookmark; `kos_dyn_finish_delimited_pop_return` restores
   invoke frame/PC and pushes the value.

Shift capture (`kos_shift_capture`) clones from `segment_top = dyn_top` with
`stop = NULL` (to chain root), detaches the bottom, and stores the template on
`KosContinuation`. `PUSH_SHIFT` captures in place (no peel-to-prompt plan).

See [callcc.md](callcc.md) § Continuation Invocation and § Plan structure
(delimited / PUSH).

---

## Problem: double wind/unwind on in-shift invoke

When `k` is invoked **inside** the same live `reset`/`shift` tree where it was
captured, PUSH grafts a **clone** of the captured template (including heritage
`KOS_DYN_WIND` nodes) above the DELIM bookmark while the **live** heritage is
still entered from natural entry. The PUSH plan replays wind `EXTENT_ACTION`s on
the clone without an intervening unwind on the live copy.

Observed lifecycle (wrong):

```
WIND_live     — natural entry into reset / dyn-wind toward shift
WIND_clone    — PUSH plan before-thunks on grafted template
... work ...
UNWIND_clone  — unwind grafted segment on POP_PROMPT return path
UNWIND_live   — POP_SHIFT / POP_PROMPT / natural reset exit
```

Desired lifecycle:

```
WIND_live → (transfer into k, no re-wind) → work → UNWIND_live → deliver at call site
```

Alternating **WIND, UNWIND** per logical extent — not **WIND, WIND, UNWIND,
UNWIND**.

**Remote invoke** (captured `k` used after the original reset finished) masks
the bug: natural WIND/UNWIND already completed before invoke; PUSH wind runs
once on a context with no duplicate live heritage below DELIM.

| Scenario | Prompt in live ancestry? | PUSH wind collision? |
|----------|--------------------------|----------------------|
| In-shift `(shift k (call k val))` | Yes | Yes — double wind |
| Remote `(let [[k (reset ...)]] ... (call k val))` | No | No — single wind on graft |

Ref: `test_dyn_wind_around_shift_invoke_inside_shift_body`,
`test_dyn_wind_around_shift_invoke_after_capture` in `test_reset_shift.py`.

---

## Design goals

1. **Token check** remains the evaluator in-context gate (sufficient; no
   separate Python-boundary check beyond existing `kos_validate_capture`).
2. **Cheap return path** — bookmark invoke site (DELIM or equivalent fields on
   `KOS_DYN_PROMPT`); do **not** allocate a `KosContinuation` for the invoke
   fragment.
3. **Single wind/unwind per extent** — especially when `k` is invoked inside its
   own shift body.
4. **Natural reset completion** — shift body finishes, unwind progresses
   through live extents, reset/prompt boundary yields TOS, value delivered to
   bookmarked call site.
5. **Avoid full-chain capture** unless required — shift template should be
   prompt-bounded (+ explicit heritage policy), not `dyn_top → root` solely to
   support graft.

---

## Target invoke flow (conceptual)

At `(call k val)` for delimited `k`:

1. Validate token.
2. Bookmark return (frame, PC, stack slot) — DELIM today; optionally PROMPT
   return slot later.
3. **Enter shift continuation** — strategy depends on prompt lineage (below).
4. Inject `val`.
5. Evaluate until reset/prompt boundary; TOS is the result.
6. Resume bookmarked call site with that value.

Long-term classical shape (for reference):

- Capture invoke continuation → transfer to shift continuation → store return on
  reset node → run → POP_PROMPT → invoke return continuation at call site.

**In-between target (preferred):** keep bookmarked return (no invoke
continuation object); fix shift **entry** so wind is not replayed on overlapping
live heritage.

---

## Two-path invoke strategy (core decision)

**Selector:** is the continuation's `prompt_id` present in the ancestry of
current `dyn_top`? (`kos_find_prompt(es, prompt_id)` from `dyn_top` toward
root.)

### Path A — prompt in lineage (in-shift / same reset)

Invoke site is still under the live `reset` where `k` was captured. Heritage
extents may already be entered.

**Do not** run `kos_build_delimited_push_plan` (no graft, no template wind
replay).

**Do:**

1. Push return bookmark (DELIM or PROMPT slot).
2. Inject `val` on the appropriate stack.
3. Resume at shift continuation `resume_frame` / `resume_pc` / `resume_stack` on
   the **existing live chain** (minimal transfer — inject + resume in place).

Completion unwinds **once** through live extents; reset delivers value to the
bookmark.

### Path B — prompt not in lineage (remote invoke)

Original reset finished; no live overlap with captured heritage.

**Use PUSH path** (current machinery, with bounded template + PUSH-then-wind +
`xfer_remap_extent_nodes`):

1. Return bookmark (DELIM).
2. `kos_build_delimited_push_plan` — graft bounded captured segment above
   bookmark, wind once on template, inject `val`.
3. Run until template `POP_PROMPT`; unwind graft; finish via bookmark.

No double-wind collision because live heritage from capture time is gone.

```
(call k val):
  if kos_find_prompt(es, k->prompt_id):
    bookmark + inject + resume in place on live chain
  else:
    bookmark + delimited PUSH plan (bounded template)
```

---

## Return bookmark vs continuation capture

`KOS_DYN_DELIM` today is **not** a captured continuation. It stores resume
metadata only; the invoke-site chain below the bookmark stays live. That is the
intended cheap return mechanism and should be preserved.

Optional future cleanup: move bookmark fields onto `KosDynPrompt` during `k`
invoke so reset is the explicit return hub; `POP_PROMPT` reads the slot
directly. Semantics unchanged — still not a `KosContinuation` clone.

---

## Shift capture (target)

Separate from invoke, but coupled:

- Capture should produce a **prompt-bounded** template (+ heritage nodes between
  SHIFT and PROMPT in the bytecode extent tree when dyn-wind wraps shift).
- Avoid cloning capture-time caller frames above PROMPT unless a future review
  proves they are required.
- `callcc.md` “clone to prompt (exclusive)” is closer to the target than
  `stop = NULL` to root; reconcile with heritage policy explicitly in code and
  tests.

---

## Relation to undelimited invoke

| | Undelimited `let/cc` | Delimited (target) |
|---|---------------------|-------------------|
| Entry | INSTALL + unwind to LCA | Path A: resume in place; Path B: PUSH graft |
| Return | No return to invoke site | Bookmark → call site |
| In-context | Token | Token |
| Chain overlap | LCA + graft_point_id | Prompt in ancestry |

Delimited nested-interrupt forbid (§6.6 in interrupted-transfer-plans.md) applies
to PUSH plans; in-shift Path A (no PUSH plan) may reduce nested PUSH replan
pressure but policy should be revisited after Path A exists.

---

## Implementation notes (when started)

**Likely touch points:**

| Area | Change |
|------|--------|
| `kos_invoke_continuation` | Branch on prompt-in-lineage; Path A vs Path B |
| `kos_shift_capture` | Prompt-bounded segment; heritage gating |
| `kos_build_delimited_push_plan` | Path B only; bounded template |
| `POP_PROMPT` / `kos_dyn_finish_delimited_pop_return` | Optional PROMPT return slot |
| `design/interpreter/callcc.md` | Mark PUSH-only as current; link here for target |

**Suggested test matrix:**

| Test | Path | Expect |
|------|------|--------|
| `test_dyn_wind_around_shift_invoke_inside_shift_body` | A | Single before-thunk; result 5 |
| `test_dyn_wind_around_shift_invoke_after_capture` | B | Single wind on graft; result 5 |
| `test_dyn_wind_around_shift_captures_in_place` | capture | Unchanged |
| `TestDelimitedTransferNestedForbid` | B / nested PUSH | No regression |

**Build / run:**

```bash
make build-ext
tox -qe py -- tests/compile/specials/test_reset_shift.py -v
```

---

## Explicit non-goals (for this milestone)

- Cloning invoke-site continuation into `KosContinuation` on every `k` invoke.
- Transactional/scratch-space transfer (see interrupted-transfer-plans §3).
- Full delimited PUSH replan (§6.6 forbid remains until real PUSH replan exists).
- Changing token / `KoskariContinuationApply` policy for undelimited `call/cc`.

---

## Open questions

1. **Path A detection** — is prompt-in-ancestry sufficient, or do we also need
   to verify invoke site lies inside the captured segment (node_id / frame
   walk)?
2. **PROMPT return slot vs DELIM** — relocate bookmark to prompt node or keep
   DELIM chain marker?
3. **`resume_pc`** — align `kos_shift_ic_for` (SHIFT `pop_pc+1`) with
   `POP_SHIFT` PROMPT-ancestor walk when heritage sits between SHIFT and PROMPT.
4. **Path B template bound** — exact `stop` node for `kos_clone_chain_segment`
   at capture time.

---

## Summary

| Layer | Today | Target |
|-------|-------|--------|
| In-context | Token | Token (unchanged) |
| Return | DELIM bookmark | DELIM or PROMPT slot (still not `KosContinuation`) |
| In-shift invoke | PUSH graft → double wind | Inject + resume on live chain |
| Remote invoke | PUSH graft | PUSH graft (bounded template, single wind) |
| Capture | `dyn_top → root` | Prompt-bounded + heritage |
| Completion | POP_PROMPT → DELIM unwind | Natural unwind → bookmark |

The organizing axis is **prompt in current lineage**: same reset → no graft; no
reset in ancestry → PUSH.
