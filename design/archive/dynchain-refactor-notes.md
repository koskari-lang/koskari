# Dynchain Refactor Notes

**Status:** Mostly landed; remaining items are deliberate or low priority
**Related:** dynchain-flatten-proposal.md (archive), generic-dynnode-protocol.md, koskari/lib/dynchain.c, koskari/lib/eval.c

These notes tracked violations of the design goal: move all chain behavior from the evaluator loop into generic interface functions that switch/case dispatch to type-specific handlers. The 2026-07 evaluator/dynchain cleanup passes resolved nearly all of them; this file now records what landed and what was deliberately kept.

---

## Resolved

- **eval.c exception handling** — `try_local_exception_handler` and its inline TRY/FINALLY logic are gone. eval.c contains no `KosDynNode_IS_TRY` / `IS_FINALLY` checks; the raise walk lives in `kos_dyn_try_local_raise_body` (dyn_walk.c) and routes through `kos_dyn_handle`. eval is fully type-agnostic.
- **Generic uninstall path** — pops funnel through the `kos_dyn_pop_nonbarrier_top` / `kos_dyn_pop_top_allow_barrier` / `kos_dyn_pop_top` entry points into the static `kos_dyn_uninstall_core` (handler, unlink, free). The dead public `kos_dyn_uninstall` wrapper and its `kos_dyn_pop` alias were removed.
- **Opcode pop if/else chains** — specialized opcodes (POP_TRY, POP_FINALLY, POP_LOOP, POP_FOREACH, CLEAR_*) assume the top node kind guaranteed by compilation and call their kind's pop directly; the old if/else dispatch chains are gone.
- **`dynnode_traverse_region`, `kos_find_node_for_raise`** — no longer exist; GC traverse dispatches per kind, and raise routing goes through `kos_dyn_handle`.
- **Thin wrappers** — `kos_dynnode_run_wind`, `kos_dynnode_run_transfer_unwind`, `kos_dyn_pop_one_unwind_walk`, and `finalize_provisional_with_enter` deleted; pop-result mapping consolidated in `kos_map_pop_result`.

## Deliberately kept

- **`kos_dyn_handle` inline transit predicates** — the TRY/FINALLY/GEN/PROMPT consume-vs-pass rules are written inline in its switch (dynchain.c). This is the single authoritative transit-rules table; keeping the short predicates in one place reads better than scattering them into the per-kind fragments.
- **Find-by-kind lookups** — `kos_find_node_for_loop` (IS_LOOP/IS_FOREACH), `kos_find_prompt` (IS_PROMPT), `kos_find_nearest_delim`. These are "find by kind" APIs; kind checks are their contract. Capability markers (e.g. a LOOP_TARGET cap) remain an option if the chain ever goes capability-only, but nothing requires it.
- **IS_FRAME / IS_WIND structural checks** — validation of structural invariants in frame operations and transfer-plan logic; retained as debug/consistency guards.
