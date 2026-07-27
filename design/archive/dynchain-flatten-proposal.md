# Dynchain Flatten and Simplify: Design Proposal

**Status:** Core goals implemented (archived 2026-06-22). Region kinds are
separate `KosDynKind` values with per-kind union slots; see
[extent-wind-unwind.md](../interpreter/extent-wind-unwind.md) and
`koskari/lib/dynchain.h`.
**Author:** —
**Related:** [dynamic-node-unified-api.md](dynamic-node-unified-api.md) (archived), [extent-wind-unwind.md](../interpreter/extent-wind-unwind.md), `koskari/lib/dynchain.c`, `koskari/lib/dynchain.h`

This document proposed a refactor of the Koskari dynamic chain to reduce complexity, eliminate redundant branching, and make behavior easy to locate.

## Implementation Notes (Post-Execution)

The core of this proposal — replacing `KOS_DYN_REGION + KosRegionStyle` with distinct top-level
kind values — **has been implemented**. `KOS_DYN_TRY`, `KOS_DYN_FINALLY`, `KOS_DYN_LOOP`, and
`KOS_DYN_FOREACH` now exist as separate enum values with separate union slots (`try_`, `finally_`,
`loop`, `foreach`) and per-kind predicate macros (`KosDynNode_IS_TRY`, etc.).

**Adopted:**
- §1 Goal 1 (flatten region kinds): implemented.
- §1 Goal 2 (minimal payloads): implemented — separate union slots per kind.
- §2 KosDynKind enumeration: matches the implementation exactly.
- §3 Union layout: implemented with the per-kind struct layout described.
- §9 Opcode changes: PUSH_TRY, PUSH_FINALLY, PUSH_LOOP, PUSH_FOREACH each push the corresponding kind.
- §7 File layout / discoverability: implemented via labeled sections in `dynchain.c`.
- §10 Migration steps 1–4: completed.

**Not adopted:**
- §4/§8 Generic operation names `kos_dyn_install` / `kos_dyn_uninstall`: the existing
  `kos_dyn_push_*` / `kos_dyn_pop_*` / `kos_dyn_uninstall_handler` naming was retained. The
  `kos_dyn_install` and `kos_dyn_uninstall` entry points described in §4 do not exist under
  those names.
- §4 `kos_dyn_unwind_on_exception` rename: the existing name `kos_dyn_unwind_on_exception` was
  not renamed to `kos_dyn_exception_unwind`.
- §6 No-kind-checks-outside-generic-functions: partially achieved. `KosDynNode_IS_REGION` now
  exists as a macro alias checking all four concrete kinds; there are still inline kind checks
  in some opcode and eval paths (see `design/interpreter/dynchain-refactor-notes.md`).
- §10 Steps 5–7 (remove `KosRegionStyle`, merge teardown into unwind, rename pop→uninstall):
  `KosRegionStyle` is still present as a vestigial typedef in `dynchain.h`.

**Superseded:** See `design/EVALUATOR.md` and `design/INVARIANTS-AND-CLARIFICATIONS.md`
for the current accurate description of the dynamic chain kinds.

---

## 1. Goals

1. **Flatten region types** — Replace `KOS_DYN_REGION` + `KosRegionStyle` with distinct kinds: `KOS_DYN_TRY`, `KOS_DYN_FINALLY`, `KOS_DYN_LOOP`, `KOS_DYN_FOREACH`.
2. **Minimal payloads** — Each kind's union slot contains only the fields it needs.
3. **Single dispatch boundary** — Generic functions use a switch on `KosDynKind` and call kind-specific handlers; no other code branches on kind.
4. **Merge unwind + teardown** — Fold `teardown_after_unwind` into `unwind`; each kind's unwind handler does all cleanup (semantic exit + structural teardown).
5. **Rename pop → uninstall** — Align with install/uninstall terminology (push/pop is already used for stack).
6. **Exception unwind** — Keep `exception_unwind`; ensure it is the single entry point when unwinding during exception propagation.
7. **Clone all kinds** — Extend clone to cover every kind (CC_RAISE remains non-cloneable).
8. **Discoverability** — Structure the codebase so that locating the implementation of any generic behavior for a given kind is immediate.

---

## 2. KosDynKind (Flattened)

```c
typedef enum KosDynKind {
  KOS_DYN_FRAME = 0,
  KOS_DYN_PROMPT,
  KOS_DYN_WIND,
  KOS_DYN_TRY,         /* was KOS_REGION_TRY */
  KOS_DYN_FINALLY,     /* was KOS_REGION_FINALLY */
  KOS_DYN_LOOP,        /* was KOS_REGION_LOOP */
  KOS_DYN_FOREACH,     /* was KOS_REGION_FOREACH */
  KOS_DYN_NS,
  KOS_DYN_CC,
  KOS_DYN_YIELD_FROM,
  KOS_DYN_PARAM,
  KOS_DYN_WITH,
  KOS_DYN_DYN_WITH,
  KOS_DYN_SHIFT,
  KOS_DYN_CC_RAISE,

  KOS_DYN_UNKNOWN,
} KosDynKind;
```

`KosRegionStyle` is removed. All region-like behavior is expressed via Kind.

---

## 3. Union Layout: Kind-Specific Payloads

Each kind gets only the fields it uses. Superset layout is eliminated.

| Kind | Fields |
|------|--------|
| **TRY** | `handler_pc` (uint32_t), `enabled` (uint8_t) |
| **FINALLY** | `handler_pc` (uint32_t), `enabled` (uint8_t), `handling_raise` (uint8_t) |
| **LOOP** | `break_pc` (uint32_t), `continue_pc` (uint32_t), `enabled` (uint8_t), `result` (PyObject *) |
| **FOREACH** | `break_pc`, `continue_pc`, `enabled`, `result`, `iter` (PyObject *), `exhausted` (uint8_t), `iter_next_else_pc` (uint32_t) |

Other kinds keep their current layouts. The union `u` in `KosDynNode` gains new slots:

```c
struct { uint32_t handler_pc; uint8_t enabled; } try_;
struct { uint32_t handler_pc; uint8_t enabled; uint8_t handling_raise; } finally_;
struct { uint32_t break_pc; uint32_t continue_pc; uint8_t enabled; PyObject *result; } loop;
struct {
  uint32_t break_pc;
  uint32_t continue_pc;
  uint8_t enabled;
  PyObject *result;
  PyObject *iter;
  uint8_t exhausted;
  uint32_t iter_next_else_pc;
} foreach;
```

**Allocation:** All kinds share a single allocation size (the union is as large as its largest member). The freelist remains unified. Each kind's struct is padded as needed to match max size.

**Accessors:** Kind-specific macros, e.g. `KosDynTry_HANDLER_PC`, `KosDynFinally_HANDLING_RAISE`, `KosDynLoop_BREAK_PC`, etc.

---

## 4. Generic Operations (Single Dispatch)

The following are the **only** operations that switch on `KosDynKind`. All branching on kind is confined to these functions.

| Operation | Signature | Purpose |
|-----------|-----------|---------|
| **install** | `int kos_dyn_install(ctx, node)` | After push: establish extent |
| **uninstall** | `int kos_dyn_uninstall(ctx, node, out_ev)` | Before unlink: tear down extent (renamed from pop) |
| **wind** | `int kos_dyn_wind(ctx, node)` | Transfer: re-enter extent |
| **unwind** | `int kos_dyn_unwind(ctx, node, out_awaitable)` | Transfer: semantic exit + structural teardown (merged) |
| **exception_unwind** | `int kos_dyn_exception_unwind(ctx, node, out_ev)` | Exception propagation: handle or pass through |
| **clone** | `KosDynNode *kos_dyn_clone(node)` | Copy for continuation capture |

**Capability removal:** `KOS_DYN_CAP_NEEDS_TRANSFER_TEARDOWN` is removed. Teardown is part of unwind.

---

## 5. Per-Kind Handler Matrix

| Kind | install | uninstall | wind | unwind | exception_unwind | clone |
|------|---------|-----------|------|--------|------------------|-------|
| FRAME | — | unlink | — | — | — | yes |
| PROMPT | push stack block | drop stack block | — | drop stack block | drop + unlink, return 0 | yes |
| WIND | — | run after | before | after | — | yes |
| TRY | push stack block | drop stack block | — | drop stack block | dispatch to handler (internal) | yes |
| FINALLY | push stack block | drop stack block | — | drop stack block | dispatch to handler (internal) | yes |
| LOOP | push stack block | drop stack block | — | drop stack block | — | yes |
| FOREACH | push stack block | drop stack block | — | drop stack block | — | yes |
| NS | — | restore ns | — | — | — | yes |
| CC | — | unlink | — | — | — | yes |
| YIELD_FROM | — | close delegate | — | — | — | yes |
| PARAM | enter | restore | enter | restore | — | yes |
| WITH | enter | exit + drop block | — | exit + drop block | (via uninstall, exit may swallow) | yes |
| DYN_WITH | enter | exit + drop block | enter | exit + drop block | (via uninstall) | yes |
| SHIFT | push stack block | — | — | — | — | yes |
| CC_RAISE | — | — | — | re-raise | — | **no** |

**Unwind (merged):** PROMPT and DYN_WITH perform structural teardown (drop stack block) inside their unwind handler. No separate teardown phase.

**Exception unwind:** TRY and FINALLY implement dispatch-to-handler internally (clear block, set handling_raise for FINALLY, set PC, return "swallowed"). PROMPT drops block and unlinks. WITH/DYN_WITH are handled via uninstall (which runs exit); if exit swallows, unwinding stops. Exception propagation uses `exception_unwind` only when normal uninstall is refused (BARRIER) or when the node is an exception handler (TRY/FINALLY).

---

## 6. No Kind Checks Outside Generic Functions

**Rule:** Once control leaves the generic dispatchers (install, uninstall, wind, unwind, exception_unwind, clone), no function shall branch on `KosDynNode_KIND(node)`. Handlers receive a node already known to be their kind (via the switch that dispatched to them).

**Current violations to fix:**

- `kos_find_region_for_raise` and `kos_find_region_for_loop` check `KosDynNode_IS_REGION` then `KosDynRegion_STYLE`. These become `kos_find_node_for_raise` and `kos_find_node_for_loop` that check `KIND == KOS_DYN_TRY || KIND == KOS_DYN_FINALLY` and `KIND == KOS_DYN_LOOP || KIND == KOS_DYN_FOREACH` respectively. The finders return `KosDynNode *`; callers that need kind-specific data use the appropriate accessor after verifying the returned kind (or the finder returns a union type / tagged pointer).
- Opcodes (POP_TRY, POP_FINALLY, etc.) today verify `KosDynRegion_STYLE(region) != KOS_REGION_TRY`. With flattened kinds, they verify `KosDynNode_KIND(node) != KOS_DYN_TRY`. The opcode is already specialized (POP_TRY); it may assume the top is TRY if the compiler guarantees it. Verification can stay as a debug check.
- `KosDynNode_IS_REGION` and similar predicates: replace with `KosDynNode_IS_TRY`, `KosDynNode_IS_FINALLY`, etc., or remove in favor of direct Kind checks in the few places that need them (the finders and generic dispatchers).

**Capability bits:** Retain only those used for control flow the generic layer must know: `KOS_DYN_CAP_BARRIER`, `KOS_DYN_CAP_HANDLES_EXCEPTIONS`, `KOS_DYN_CAP_NEEDS_UNWIND_ACTION`, `KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION`, `KOS_DYN_CAP_NEEDS_INSTALL_WIND`, `KOS_DYN_CAP_CATCH`, `KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP`. Remove `KOS_DYN_CAP_NEEDS_TRANSFER_TEARDOWN`.

---

## 7. File Layout for Discoverability

**Generic dispatch must be implemented via switch/case on the kind value.** A function pointer table or handler registry is not acceptable; the implementation must branch explicitly on `KosDynKind` in each generic operation.

To make it trivial to find the implementation of any generic behavior for a given kind, use a single `dynchain.c` with labeled sections:

```c
/* === KOS_DYN_TRY === */
static int kos_dyn_try_install(...) { ... }
static int kos_dyn_try_uninstall(...) { ... }
...
/* === KOS_DYN_FINALLY === */
...
```

And a header comment mapping Kind → line range or section.

---

## 8. API Renames

| Old | New |
|-----|-----|
| `kos_dyn_pop` | `kos_dyn_uninstall` (or keep `kos_dyn_pop` as alias for compatibility during migration) |
| `kos_dyn_pop_handler` | `kos_dyn_uninstall_handler` (internal) |
| `kos_dyn_teardown_after_unwind` | removed; logic folded into `kos_dyn_unwind` |
| `kos_dyn_unwind_on_exception` | `kos_dyn_exception_unwind` |
| `kos_dyn_push_region` | replaced by `kos_dyn_push_try`, `kos_dyn_push_finally`, `kos_dyn_push_loop`, `kos_dyn_push_foreach` |
| `kos_dyn_pop_region` | replaced by kind-specific uninstall (opcodes call generic `kos_dyn_uninstall`) |
| `kos_find_region_for_raise` | `kos_find_node_for_raise` (returns `KosDynNode *`, caller checks Kind) |
| `kos_find_region_for_loop` | `kos_find_node_for_loop` |

---

## 9. Opcode and Eval Changes

- **PUSH_TRY, PUSH_FINALLY, PUSH_LOOP, PUSH_FOREACH** — Call `kos_dyn_push_try`, `kos_dyn_push_finally`, etc., which allocate the correct kind and call `kos_dyn_push` (which calls install).
- **POP_TRY, POP_FINALLY, POP_LOOP, POP_FOREACH** — Verify top has the expected kind, perform PopBlock if needed (for TRY/FINALLY), then call `kos_dyn_uninstall` (or a thin wrapper that does PopBlock + uninstall for TRY/FINALLY).
- **CLEAR_TRY, CLEAR_FINALLY, CLEAR_LOOP, CLEAR_FOREACH** — Set `enabled = 0` on the appropriate kind. No kind check needed if the opcode is already specialized.
- **BREAK, CONTINUE** — Use `kos_find_node_for_loop`; cast to `KosDynLoop` or `KosDynForeach` based on Kind.
- **ITER_NEXT** — Use finder for FOREACH only.
- **eval.c exception dispatch** — Uses `kos_find_node_for_raise`; dispatches to TRY or FINALLY. The `exception_unwind` handler for TRY/FINALLY performs clear-block, set-handling-raise, set-PC internally. No separate clear-block call from eval.

---

## 10. Migration Order

1. Add new kinds (TRY, FINALLY, LOOP, FOREACH) and union slots; keep KOS_DYN_REGION temporarily.
2. Add per-kind handlers; wire them into the switch for the new kinds.
3. Update opcodes to push the new kinds; update finders to look for new kinds.
4. Migrate call sites from region+style to kind-specific.
5. Remove KOS_DYN_REGION and KosRegionStyle.
6. Merge teardown into unwind; remove NEEDS_TRANSFER_TEARDOWN.
7. Rename pop → uninstall, unwind_on_exception → exception_unwind.
8. Reorganize into clearly labeled sections.

---

## 11. Summary

| Change | Effect |
|--------|--------|
| Flatten region → TRY, FINALLY, LOOP, FOREACH | No more style checks; single Kind discriminator |
| Minimal payloads per kind | Smaller structs, clearer semantics |
| Teardown folded into unwind | One less operation, one less capability |
| pop → uninstall | Consistent install/uninstall naming |
| Kind checks only in generic layer | No scattering of type tests |
| Per-kind sections | Immediate discoverability of behavior |

The end state: six generic operations (install, uninstall, wind, unwind, exception_unwind, clone), each a single switch on Kind dispatching to kind-specific handlers. No other code branches on kind.
