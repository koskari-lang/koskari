# Koskari — Static Bytecode Metadata Tables (Locations + Extents)

**Status:** Implemented (archived 2026-06-22). Canonical summary:
[interpreter/bytecode.md](../interpreter/bytecode.md) (extent table, location API).

---

This document records design decisions to move from **dynamic, opcode-driven block push/pop** patterns toward **statically compiled metadata tables** attached to a code object. Extent table, location API, JUMP locality validation, and evaluator integration for TRY are in `koskari/lib/bytecode.c`.

Primary goals:

* Make key control targets available in **O(1)** (or amortized O(1)) from **pc** (instruction index).
* Eliminate repeated "hunt forward" scans in the evaluator for:

  * shift/reset continuation return points ("ic at end of shift body")
  * try/catch/finally handler targets
  * unbounded continuation bind bodies needing an exterior "return-to" destination
* Keep the hot evaluator loop compact: `uint16 op, uint16 arg` instruction words.

---

## 0. Change Summary

### What changes

1. **Structural control metadata** (block extents, handler targets, continuation return points) is compiled into **static tables** stored alongside the bytecode in the code object allocation.
2. The evaluator stops using **forward scans** over the instruction stream to locate structural targets (IC, handler PCs, exit PCs).
3. Push/pop opcodes become **simplified**: they no longer encode "what/where" in-line beyond a compact reference (e.g. an `extent_id` or table index).

### What does *not* change

* The runtime still maintains the **dynamic chain/extent stack** (for multi-shot continuations, dynamic wind ordering, prompt boundaries, python-call boundaries, etc.).
* The bytecode format remains **narrow**: fixed-width 4B instructions (16-bit op, 16-bit arg).
* We still use **push/pop semantic opcodes** for constructs whose runtime state must exist (TRY nodes, PROMPT/RESET nodes, WIND nodes, etc.); the tables make those ops cheaper and more uniform.

---

## 1. Terms

* **pc**: instruction index into `code->insns[]`.
* **IC / return point**: a compiler-designated destination pc used as the "continuation return pc" for a block (not necessarily a `RETURN`).
* **Extent / block**: a statically known region of code with entry/exit semantics (TRY, LOOP, PROMPT/RESET, SHIFT, CALLCC-BIND, etc.).
* **Boundary pc**: a pc immediately after a PUSH-boundary or immediately before a POP-boundary for a block.
* **Compressed bytecode**: the final `op,arg` instruction stream stored in the code object.
* **Pre-compressed stream**: the compiler's initial stream, which may include compile-time opcodes like `LABEL` and `POSITION`, and may embed constants by value rather than pool indices.

---

## 2. Code Object Layout

The code object uses two read-only allocations (both page-aligned and mprotected):

1. **Main block**: bytecode array, position table, constant pool (single contiguous allocation).
2. **Extent block**: extent descriptors and `top_extent_id[pc]` array (separate allocation; omitted when no PUSH/POP blocks exist).

Contents:

* **Bytecode array**: fixed-width instructions (`op,arg`).
* **Location table** (KosPosition): mapping from pc to source `(line,col)`; fixed-width entries, binary-search lookup.
* **Extent table**:
  * `KosExtentDesc` array (ranges, kind, tagged-union kind-specific targets)
  * `top_extent_id[code_len]` per-pc lookup (Strategy B)

---

## 3. Location Table

### Decision

Use a location table keyed by pc:

* Stores only the pcs where position changes.
* Lookup by pc uses binary search.

### Implemented Shape

* `KosPosition { index, line, col }` (fixed width, 6 bytes per entry)
* C API: `kos_loc_lookup(code, pc, line, col)` (wraps `KosCode_GetPosition`)

### Rationale

* Locations are used mainly for debugging/errors/tracing and should not bloat hot instruction fetch.

---

## 4. Extent Table

### Decision

Compile all structural extents into a table so the evaluator can answer questions like:

* "If I am at pc, what is my innermost active block/extent?"
* "If this extent exits normally, what is the exterior jump pc?"
* "If an exception occurs here, what handler target should we use?"
* "If I'm at the end of a SHIFT body, what pc do I jump to next?"

### Minimal block record (3 pcs)

We can represent each block/extent with **three pcs**:

* `push_pc`: the PUSH instruction that begins the block
* `enter_pc`: the first instruction *inside* the block (`push_pc + 1`)
* `pop_pc`: the POP instruction that ends the block and acts as the "gateway" to the parent

**Inside predicate (half-open interval):**

* A pc is **inside** the block iff: `enter_pc <= pc < pop_pc`

**POP is leaving:**

* `pc == pop_pc` is treated as **outside** the block (at the gateway).
* Jumping to `pop_pc` means "leave the block" (no re-entry into the block).

This model makes block membership and legality checks mechanical.

### Relationship to push/pop opcodes

We **retain push/pop semantic opcodes**, but simplify them.

Two equivalent encoding options are permitted:

* **By `extent_id` immediate**: PUSH/POP carry an `extent_id`.
* **By boundary/IC inference**: PUSH/POP carry (or imply) a **boundary pc / IC pc**, and the evaluator resolves the extent via an index keyed by `push_pc` and/or `ic_pc`.

In both cases:

* The *meaning* of the pushed thing (ranges, handler PCs, exit PCs, IC PCs, break/continue PCs, etc.) lives in the **extent table**.
* "Am I within X?" is derived from **pc→extent lookup**, not from scanning or sentinel patterns.

### Extent Descriptor

An extent descriptor is immutable and references pcs by index. It is a **tagged union**: common fields plus a union of kind-specific target structs. Use the union member that matches `kind`; the C API and `kos_try_handler_for` / `kos_shift_ic_for` do this for you.

Common fields:

* `kind`: TRY, LOOP, RESET/PROMPT, SHIFT, CALLCC_BIND, etc.
* `push_pc`
* `pop_pc`
* `parent_extent_id` (nesting)
* `flags`

Derived:

* `enter_pc = push_pc + 1`

Kind-specific payload (union `u`; use the variant for the extent kind):

* TRY, FINALLY: `u.try_finally` — `except_pc`, `else_pc`, `finally_pc`, `exit_pc` (use 0xFFFF for none)
* LOOP, FOREACH: `u.loop` — `break_pc`, `continue_pc`
* WITH, CALLCC_BIND: `u.exit_only` — `exit_pc`
* SHIFT: `u.shift` — `ic_pc` (the "end-of-shift" continuation pc)
* Other kinds: no targets; union is still initialized (e.g. to NONE) for consistency

### pc→Extent Lookup

**Implemented: Strategy B** (direct per-pc top-extent table):

* `top_extent_id[pc]` gives the innermost extent id at each pc (or `KOS_EXTENT_NONE`).
* Extents are derived during Code construction from PUSH/POP pairs; nesting is validated.
* Code with no PUSH/POP blocks has `extent_len == 0`, `extents == NULL`, `top_extent_id == NULL`; lookups return NONE.

---

## 5. Evaluator Integration

### Decision

The evaluator must be able to query extents by pc with bounded overhead.

At minimum it must support:

* `extent_top(pc)` (innermost)
* `extent_find_kind(pc, kind)` (nearest enclosing of type)
* `try_handler_for(pc)` (innermost TRY and its handler targets)

The evaluator should use cached state when stepping forward sequentially.

### Block table is the source of truth

At any `pc`, the block/extent table is the authoritative answer for:

* which extents are active
* what cleanup/handler targets apply

The runtime chain must be made consistent with this structural truth during normal stepping, exception transfer, and continuation application.

### No more "hunt forward"

Any place the evaluator previously scanned forward to find a sentinel or a matching POP marker now becomes:

* a table lookup
* or a jump to a known pc

---

## 6. Jump Legality and Locality

### Decision: JUMP is strictly local

**Non-local jumps are forbidden** for control-flow jump opcodes. During Code construction we validate that JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, and all CHECK_* opcodes do **not** target a pc outside the current active block. ITER_NEXT must be inside a PUSH_FOREACH block.

**Excluded from validation**: PUSH_TRY, PUSH_FINALLY, PUSH_LOOP, PUSH_WITH, etc. Their args are handler/target labels that may legitimately point outside the block (e.g. exception handler after POP_TRY).

Definitions:

* `src_block = block_top(src_pc)` (innermost active block at the source pc)
* A target `t` is considered **inside** `src_block` iff: `src_block.enter_pc <= t < src_block.pop_pc`

Rules:

* A `JUMP` may target any `t` inside the source block.
* A `JUMP` may also target `t == src_block.pop_pc` to **leave the block** via the POP gateway.
* (Optional) A `JUMP` may target `t == src_block.enter_pc` to "re-enter at boundary" if you want to permit tight loop patterns without a dedicated opcode.
* A `JUMP` may not target a pc whose `block_top(t)` differs from `src_block`, except as allowed above.

This ensures cleanup correctness because leaving a block is always expressed as "go to POP," and POP is the single transition point.

### Transfers that *can* be non-local

Non-local control transfers exist, but they are *not* `JUMP`:

* exception handling transfer
* continuation invocation / resumption
* other VM "special transfer" modes

These transfers run explicit cleanup modes (pop blocks to handler, unwind winds, clear loop blocks before restart, etc.) and/or fully restore stack/extent state as part of resumption.

---

## 6. Evaluator Step Order (Invariants)

To avoid off-by-one ambiguity, the evaluator uses a consistent step order:

1. `pc = current instruction index`
2. `active = block_top(pc)` using the half-open interval rule (`enter <= pc < pop`)

   * In particular, `block_top(pop_pc)` must resolve to the **parent** (POP is "leaving").
3. Execute instruction at `pc`.
4. If the instruction is `POP`, it must match the table's `pop_pc` for the block that was active at `pc-1` and transitions to the parent.

Exception dispatch and other transfers compute block membership using the same definition.

---

## 7. Compiler Responsibilities

### Decision

The compiler builds the final code object in a two-stage flow:

1. Emit a **pre-compressed** stream that may include compile-time opcodes (`LABEL`, `POSITION`) and embed constants by value.
2. Run a **final compression/finalize** pass that:

   * resolves labels → pcs
   * compacts constants → pool indices
   * derives the **extent table** and **location table** from the finalized instruction stream

This is analogous to how label positions are identified and converted during finalize.

Required compiler outputs after finalize:

1. Compressed instruction stream (`op,arg`).
2. Location events (pc→line/col changes).
3. Extent descriptors with accurate:

   * range boundaries (start/end)
   * parent nesting
   * all control targets (exit/handlers/ic/break/continue)
4. pc→extent lookup structure (chosen strategy A or B).

### Structural validation

Finalize must validate:

* extents are properly nested
* targets are within valid ranges
* `start_pc < end_pc`
* handler targets point to valid instruction boundaries
* **JUMP locality** rules are satisfied (no jump exits extent; only boundary crossings allowed)

### Table coherency invariant

* Tables are **derived artifacts** of the final instruction stream.
* If the instruction stream changes, tables must be rebuilt atomically.

---

## 8. Use-Case: PUSH_SHIFT / POP_SHIFT and end-of-shift IC jump

### Current behavior

* PUSH_SHIFT/POP_SHIFT form an extent of kind `KOS_EXT_SHIFT` with `pop_pc` at POP_SHIFT.
* The extent's `ic_pc` (continuation resume point) is `pop_pc + 1` (first instruction after POP_SHIFT).
* POP_SHIFT uses `kos_extent_find_by_pop_pc(code, pop_pc, KOS_EXT_SHIFT)` to find the extent (since at pop_pc, `kos_extent_top` returns NONE).
* PUSH_SHIFT uses `kos_shift_ic_for(code, pc)` at push_pc to get `pop_pc + 1` for the captured continuation.

Opcode handlers receive `CURRENT_ACTING_PC(ctx)` (the instruction being executed); the dispatch loop advances `pc` before dispatch, so extent lookup uses acting_pc rather than `pc - 1`.

---

## 9. Use-Case: TRY push knows handler targets

### Current pain

TRY entry may require scanning forward to determine where to jump on exception, or to locate finally/else boundaries.

### New behavior

TRY compilation emits:

* TRY extent descriptor with `u.try_finally` (except_pc, else_pc, finally_pc, exit_pc)

Evaluator TRY entry:

* pushes a runtime TRY node (dynamic chain) referencing the extent id
* exception dispatch uses the extent table directly to select handler pc

No scanning.

---

## 10. Use-Case: Unbounded call/cc bind body needs exterior destination

### Requirement

Unbounded continuations (call/cc) can be bound and then evaluate a block of subexpressions; on normal completion, control must jump to an exterior destination.

### New behavior

* The compiler emits a `CALLCC_BIND` extent for the body with `u.exit_only.exit_pc`.
* The evaluator uses that exit_pc when the bind body completes normally.

This avoids encoding "return-to" as an ad-hoc instruction search.

---

## 11. Runtime Extent Nodes vs. Static Extents

### Decision

Static extents describe *structure*.
Runtime nodes describe *dynamic state* and must still exist.

Runtime nodes should carry:

* `extent_id` (or a compact pointer/index)
* any dynamic payload (captured values, wind hooks, prompt refs, etc.)

This makes continuations stable across multi-shot capture:

* captured continuation retains runtime chain nodes
* those nodes refer back to immutable, static extent descriptors

---

## 12. Backwards Compatibility / Transition Plan

**Completed**:

1. ~~Introduce tables and wire location lookup first~~ — Done; `kos_loc_lookup` wraps `KosCode_GetPosition`.
2. ~~Convert one construct (TRY or SHIFT) to table-driven targets~~ — Done; `try_local_exception_handler` uses `kos_try_handler_for` when extent table present, falls back to `region->break_pc`.
3. Extent table is derived for all Code; no migration needed (empty code has no extents).
4. Malformed bytecode (unmatched PUSH/POP) is rejected at Code construction.

---

## 13. Resolved Decisions

* **Lookup strategy**: Strategy B (per-pc `top_extent_id`) implemented.
* **Location table**: Separate `KosPosition` table; fixed-width entries.
* **Extent id width**: 16-bit (`KOS_EXTENT_NONE = 0xFFFF`).
* **Push/pop encoding**: PUSH/POP opcodes retain current encoding (arg = label or pool index); extent table is derived from instruction stream, not from opcode args.

---

## 14. Potential Problems / Watchouts

These are not blockers, but they are common footguns in table-driven designs.

1. **Extent lookup keying (if you drop explicit extent ids)**

   * If PUSH/POP infer extents via `ic_pc`/`push_pc` mapping, ensure the mapping is **unique and total**:

     * every `push_pc` maps to exactly one extent descriptor
     * `ic_pc` values that identify "end-of-body" must not collide across unrelated extents
   * Recommended: build compact indices `extent_by_push_pc` and (where needed) `extent_by_ic_pc` during finalize.

2. **POP gateway semantics must match `block_top`**

   * The document now defines blocks as `[enter_pc, pop_pc)`.
   * Ensure all code paths (debug, tracing, exception dispatch, continuation apply) treat `pc == pop_pc` as already "outside."

3. **Resumption vs. JUMP**

   * Your rule "cannot jump into the middle; can rejoin via continuation" is sound, but requires discipline:

     * only special transfer modes may set pc arbitrarily
     * those modes must fully restore operand stack + dynamic chain invariants

4. **Exception edges**

   * "Inside TRY" for dispatch should be derived from pc→extent lookup, but the handler target must also encode which unwinds are required (especially with finally).
   * Ensure TRY descriptors fully represent the state machine (except/else/finally/exit).

5. **Finalize-pass stability**

   * Because tables are derived during finalize, the final compressed stream is the single source of truth.
   * Any subsequent transformation must be modeled as another finalize-like pass that re-derives tables.

6. **Versioning / backwards compatibility**

   * If you change extent descriptor layouts, the code object must carry a version tag and loaders must reject or adapt.

7. **Debug vs runtime tables**

   * Keep "debug-only" payload (rich source spans) separable so production builds don't pay for it.

---

## 15. Required API Surface (C) — Implemented

APIs in `bytecode.h` / `bytecode.c`:

* `kos_loc_lookup(code, pc, line, col)` — returns 0 on success, -1 on error
* `kos_extent_top(code, pc)` — extent_id or KOS_EXTENT_NONE. At pop_pc returns KOS_EXTENT_NONE (extent range is [push_pc, pop_pc)).
* `kos_extent_find(code, pc, kind)` — extent_id or KOS_EXTENT_NONE
* `kos_extent_find_by_pop_pc(code, pop_pc, kind)` — extent_id for extent with given pop_pc and kind, or KOS_EXTENT_NONE. Used by POP handlers at pop_pc.
* `kos_extent_desc(code, extent_id)` — KosExtentDesc pointer or NULL
* `kos_try_handler_for(code, pc, out)` — fills KosTryHandler; returns 0 if found, -1 otherwise
* `kos_shift_ic_for(code, pc)` — ic_pc for SHIFT extent, or KOS_EXTENT_NONE

Extent kinds: TRY, LOOP, PROMPT, SHIFT, CALLCC_BIND, FINALLY, WIND, WITH, FOREACH, BEGIN, NS. PUSH/POP pairs: TRY, LOOP, FINALLY, PROMPT, SHIFT (PUSH_SHIFT/POP_SHIFT), WITH, BEGIN, NS, WIND, REGION, FOREACH (PUSH_FOREACH/POP_FOREACH).

---

## 16. Invariants Reinforced

* Bytecode stays **read-only** after finalize.
* Extent and location tables are **immutable** and stored with the bytecode.
* No evaluator path relies on "scan forward through instruction stream" for structural targets.
* Malformed bytecode (unmatched PUSH/POP) is rejected at Code construction; non-local control-flow jumps are rejected.
