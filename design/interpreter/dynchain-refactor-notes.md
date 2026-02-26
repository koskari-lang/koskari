# Dynchain Refactor Notes

**Status:** Notes for future work  
**Related:** dynchain-flatten-proposal.md, koskari/lib/dynchain.c, koskari/lib/eval.c

These notes capture remaining violations of the design goal: move all chain behavior from the evaluator loop into purely generic interface functions that switch/case dispatch to type-specific handlers. Capability markers should replace type checks where applicable.

---

## dynchain.c Violations

### kos_find_node_for_raise (lines 2041–2050)

Uses explicit kind checks:
`KosDynNode_IS_TRY(node) && KosDynTry_ENABLED(...)` or
`KosDynNode_IS_FINALLY(node) && KosDynFinally_ENABLED(...)`.

Both TRY and FINALLY have `KOS_DYN_CAP_HANDLES_EXCEPTIONS`. Replace with:
`KosDynNode_HAS_CAP(node, KOS_DYN_CAP_HANDLES_EXCEPTIONS)` plus a generic "enabled" check. The enabled accessor differs by kind (KosDynTry_ENABLED vs KosDynFinally_ENABLED), so add a generic accessor or small helper.

### kos_find_node_for_loop (lines 2053–2061)

Uses `KosDynNode_IS_LOOP(node) || KosDynNode_IS_FOREACH(node)`.

Add a capability such as `KOS_DYN_CAP_LOOP_TARGET` and use capability-based lookup instead of kind checks.

### dynnode_traverse_region (lines 3899–3907)

Uses if/else on kind:
```
if (KosDynNode_IS_FINALLY(node)) ...
else if (KosDynNode_IS_LOOP(node)) ...
else if (KosDynNode_IS_FOREACH(node)) ...
```

Replace with `switch (KosDynNode_KIND(node))` dispatching to per-kind handlers (dynnode_traverse_try, dynnode_traverse_finally, dynnode_traverse_loop, dynnode_traverse_foreach). Alternatively, have `kos_dynnode_traverse` dispatch directly to those handlers instead of going through dynnode_traverse_region.

### kos_find_prompt (2098), kos_find_cc (2131)

Use `IS_PROMPT` and `IS_CC`. Acceptable for "find by kind" APIs. If moving to capability-only, add capabilities (e.g. KOS_DYN_CAP_PROMPT).

### IS_WIND checks (2569, 2620)

Used in transfer-plan logic. Could use a capability (e.g. KOS_DYN_CAP_WIND) if moving fully capability-based.

### IS_PROMPT checks (1991, 2021)

In transfer/unwind code. Same consideration as finders.

### IS_FRAME checks (283, 342, 1592, 1608, 1634, 2238, 2275, 2347, 2439)

Structural invariants for frame-specific operations. May remain as debug/validation checks.

---

## eval.c Violations

### try_local_exception_handler (lines 2668, 2677, 2686, 2692, 2702)

Uses `KosDynNode_IS_TRY(region)`, `KosDynNode_IS_FINALLY(region)` and implements clear-block, set-PC, enabled, HANDLING_RAISE directly.

Per proposal: "The exception_unwind handler for TRY/FINALLY performs clear-block, set-handling-raise, set-PC internally." Move this logic into dynchain.c's `kos_dyn_exception_unwind`. The evaluator should call `kos_dyn_exception_unwind(ctx, region, out_ev)` and the generic layer should switch to TRY/FINALLY handlers.

---

## opcode/*.c Considerations

### Repeated pop-if/else chains

Pattern appears in PUSH_TRY, PUSH_FINALLY, PUSH_LOOP, PUSH_FOREACH, POP_TRY, POP_FINALLY, POP_LOOP, PUSH_FOREACH:
```
if (KosDynNode_IS_TRY(region)) kos_dyn_pop_try(...);
else if (KosDynNode_IS_FINALLY(region)) kos_dyn_pop_finally(...);
else if (KosDynNode_IS_LOOP(region)) ...
else if (KosDynNode_IS_FOREACH(region)) ...
```

Add generic `kos_dyn_uninstall(ctx, node, out_ev)` that switch/case dispatches to the correct pop handler. Have opcodes call it instead of branching on kind.

### Specialized opcodes (POP_TRY, POP_FINALLY, POP_LOOP)

Can assume top node kind but should call the generic uninstall instead of duplicating kind checks and pop logic.

### CONTINUE, POP_LOOP, ITER_NEXT

Use IS_LOOP vs IS_FOREACH and direct KosDynLoop_* / KosDynForeach_* access. Move kind-specific behavior into dynchain.c helpers (e.g. generic get-result, set-result, get-continue_pc) or per-kind handlers invoked via switch/case.

---

## Summary of Recommended Changes

1. Move exception-handling logic from try_local_exception_handler into dynchain's kos_dyn_exception_unwind; evaluator calls it generically.
2. Replace if/else in dynnode_traverse_region with switch/case dispatch to per-kind traverse handlers.
3. Use KosDynNode_HAS_CAP(node, KOS_DYN_CAP_HANDLES_EXCEPTIONS) in kos_find_node_for_raise (plus generic enabled handling).
4. Add capability for loop-like nodes; use in kos_find_node_for_loop.
5. Add generic kos_dyn_uninstall(ctx, node, out_ev); have opcodes call it instead of branching on kind.
