# KOS_OP_MATCH_CASE

- **Style**: EXTRA. Schema (LABEL, CONST, CONST, COUNT): `a0` = no-match
  target pc, `a1` = frozen matcher tuple (cpool), `a2` = tuple of the extern
  names the matcher's defaults do not cover (cpool), `a3` = keep-subject flag.
  Matcher thawed from `a1` and a KosSlotMap of its capture slots resolved
  against the namespace layout.
- **Stack**: `[..., subject, e0 ... eN-1] → [..., subject]` on no match (then
  jump to `a0`); `→ [...]` on a match with the keep flag clear, or
  `→ [..., subject]` with it set. `N = len(a2)`: the compiler pushes one
  value per missing extern above the subject, in `a2` order.
- **Semantics**: The opcode is the interface between the stack, the
  namespace, and the matcher API. The subject goes to
  `KosMatcher_RunSlotsVectorcall` as a length-1 spine with the stacked
  externs and their names; the matcher resolves every extern (inbuilt
  defaults included) and raises its own errors. On the first match the slot
  array is bound through the cached KosSlotMap (by fast index, or by name for
  a slot without one); a slot that did not fire stays unbound. The stacked
  externs are consumed on both outcomes. With the keep flag set (a guarded
  arm) the guard code must either jump to `a0` with the subject intact or
  pop it before the arm body commits.
- **Compiler form:** Emitted by `compile/match` (`compile/specials/conditionals.py`).
- **Implementation**: `koskari/lib/opcode/MATCH_CASE.c`
