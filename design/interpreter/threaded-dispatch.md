# Function-Pointer Threaded Dispatch — Design Proposal

This document specifies a staged rework of the evaluator's instruction
dispatch. Today dispatch is a computed-goto loop that fetches a `KosOp`,
indexes an opcode label table, and jumps to an always-inlined handler body.
The end goal is a tail-call-threaded evaluator: each opcode handler tail-calls
its successor, the eval state rides in function arguments, and the dispatch
loop exists only as a trampoline that control transfers return to. This
proposal covers the intermediary that gets us most of the structure while
staying on a portable loop, and specifies the seam that lets the tail-call
form drop in later as a localized change.

**Status:** Design proposal -- not yet implemented. Decided: once phase 2
implementation begins, musttail support (clang, or GCC 15+) is mandatory
and the phase-1 loop is retired as a shipped fallback -- see §6.2. Until
that work starts, the design stays flexible as scoped below.

**See also:** [bytecode-finalization.md](bytecode-finalization.md) (the
finalize pipeline this lowering hooks into), [../archive/attr-lookup-cache.md](../archive/attr-lookup-cache.md)
(per-site cache index, the existing precedent for a load-time,
never-serialized per-instruction annotation), [EVALUATOR.md](../EVALUATOR.md),
[stencil-compilation.md](stencil-compilation.md) (copy-and-patch native
lowering built directly on this proposal's phase 2 handler form; supersedes
the AOT-to-`.so` direction record archived at
[../archive/native-compiled-modules.md](../archive/native-compiled-modules.md)).

---

## 1. Current State

### 1.1 Dispatch

`koskari_eval` (eval.c) does entry/revive normalization, transfer-plan
synchronization, then calls `koskari_eval_dispatch_computed_goto(es)`, a
single `static inline` function that holds the whole loop. The loop is
GCC/Clang computed-goto:

- `optable[]` is an array of label addresses generated from `opcodes.incl`
  via `#define OP(name,...) &&cg_TARGET_##name,`.
- `KOS_EVAL_PREPARE_NEXT_INSN` reads `KosCode_CODE(code)[pc]` into a local
  `KosOp insn`, sets `es->acting_pc = pc` and `es->pc = pc + 1`.
- `goto *optable[insn.op]` jumps to the per-opcode label.
- Each label expands from a second `OP(name,...)` pass to
  `op_res = op_##name(es, &insn)`; on `KOS_RES_NORMAL` it polls, fetches the
  next instruction, and jumps again; otherwise it falls to
  `cg_handle_op_result`.

The dispatch function must be a single function today because the label
addresses and `optable` are local to it. That constraint is the reason
`koskari_eval` and the dispatch loop are two functions rather than one.

A portability gate exists (`KOS_USE_COMPUTED_GOTO` in hints.h, default on for
GCC/Clang), but only the computed-goto dispatcher is currently present in the
tree.

### 1.2 Handler shape

Handlers are declared by `KOS_OP_HANDLER(name)`, expanding to:

```c
KOS_OP_INLINE KosOpResult op_##name(KosEvalState *es, KosOp *insn)
```

`KOS_OP_INLINE` is `static inline __attribute__((always_inline))` in the
normal build, so the handler bodies are folded directly into the dispatch
function and are not individually addressable. `KOS_OP_HANDLER_COLD` is the
`static KOS_NOINLINE` variant for large, rarely-run opcodes. Handlers already
take `(KosEvalState *es, KosOp *insn)` and read their operand via
`KosOp_ARG(insn)` and their cache slot via `KosOp_CACHE(insn)`. They return a
`KosOpResult { KosOpResultKind kind; PyObject *val; }`.

`KosOpResultKind` (eval.h): `KOS_RES_NORMAL` (continue), plus `REPROLOGUE`,
`YIELD`, `AWAIT`, `RETURN`, `RAISE`, `CONTROL_RETURNED`, `DISPATCH`, `FATAL`
for control events. The hot path is `NORMAL`; everything else routes through
`cg_handle_op_result` and `apply_kos_op_result`.

### 1.3 Instruction storage

`KosOp` is a 6-byte struct `{ uint16_t op; uint16_t arg; uint16_t cache; }`.
The instruction array `KosCode.code` lives inside the unified bytecode block:
a single allocation that finalize builds, page-aligns and seals read-only with
`mprotect` when `protect`, and can relocate into a shared `KosArena` via
`pack_into`. Its region order is `[code][positions][cpool][extra][extents][top]`
(`code_block_layout`). Two of those regions matter for this proposal: `[cpool]`
is an array of process-local `PyObject *`, and `[top]` (`top_extent_id`) is a
`uint16_t` array sized to `code_len`, a per-`pc` region parallel to `[code]`.
`.kio` and `soften` round-trip through the `(op, arg)` pairs, which finalize
re-expands into a fresh block on every load. `pc` is an index into `[code]`;
jump operands are `pc` indices set through the `KosEvalState_SET_PC` macro
(`JUMP` sets `KosEvalState_SET_PC(es, KosOp_ARG(insn))`; `CHECK_NIL` does the
same on its not-nil branch), validated in-bounds at finalize. Every code object ends in a `THEEND` barrier,
which is why the loop needs no per-opcode bounds check.

The `cache` field is the working precedent for this proposal: a per-site index
assigned at assembly, dropped by `soften`, which emits only `(op, arg)` pairs.

---

## 2. Target Model (the end state, for orientation)

Each opcode handler ends by tail-calling the handler at the successor slot,
passing the eval-state facets as arguments. The native stack stays O(1) across
a straight-line run because every successor call is a guaranteed tail call
(`musttail`). Control events (yield, await, return-across-frame, raise,
continuation capture) are the only returns: a handler that hits one
materializes the register-resident facets back into the heap `KosEvalState`
and returns a `KosOpResult` describing the event to the trampoline. The
trampoline is the merged `koskari_eval` body.

The load-bearing invariant: **no continuation-relevant state may live only in
C argument registers across a suspension point.** Every suspend/capture/return
drains the promoted facets back into `KosEvalState` before returning, so a
captured continuation always sees complete heap state. Tail-call threading
satisfies this by construction, since between control events there is no C
stack to capture and at every control event there is a return.

`musttail` is a hard dependency of this end state and is not portable (clang,
GCC 15+). The target model is therefore a build-gated dispatch backend, with
the intermediary of §3 as the portable floor. This proposal commits only to
the intermediary and the seam; the tail-call flip is scoped in §6.

---

## 3. Phase 1 — Portable Function-Pointer Dispatch

Phase 1 keeps a dispatch loop but removes the computed-goto table. Each
instruction is lowered at finalize to a widened slot carrying a direct handler
function pointer, and the loop invokes that pointer. This is portable C (no
computed goto, no `musttail`), so it also removes the GCC/Clang dispatch
dependency for the base build.

### 3.1 The extern handler table

Handlers stop being always-inlined and become addressable functions with a
uniform signature. A fresh `OP` redefinition in eval.c, sited just after the
`#include "handlers.incl"` that defines the handlers, expands the same include
into a positional array of handler pointers. Because the `OP` order in
`opcodes.incl` is the order that assigns the enum values, element position
lines up with `KOS_OP_##name`, so no designated initializers or count sentinel
are needed:

```c
typedef KosOpResult (*KosOpHandler)(KosEvalState *es, KosSlot *cur);

/* eval.c, after #include "handlers.incl" */
#define OP(name, ...) op_##name,
const KosOpHandler kos_op_handlers[] = {
  #include "opcodes.incl"
};
#undef OP
```

`kos_op_handlers` is `extern` (declared in a dispatch header reachable from
bytecode.c), because finalize in bytecode.c must read it to build a code
object's slot region. The array is defined in eval.c where the `op_##name`
functions are in scope; bytecode.c only ever indexes the array, so the handlers
staying `static` is fine. `KOS_OP_HANDLER` drops `always_inline`; the handlers
become ordinary functions whose address the table takes. Cold handlers are
unaffected (already `KOS_NOINLINE`).

### 3.2 The widened slot (`KosSlot`)

The dispatch record is a per-`pc` region in the unified block, parallel to
`[code]` and indexed by the same `pc`, added to `code_block_layout` beside the
existing `top_extent_id` region:

```c
typedef struct KosSlot {
  KosOpHandler fn;    /* resolved from kos_op_handlers[op] at finalize */
  uint16_t op;        /* decoded copy, for introspection/asserts */
  uint16_t arg;       /* decoded operand */
  uint16_t cache;     /* per-site cache slot index (or KOS_CACHE_UNSET) */
} KosSlot;
```

Carrying `arg`/`cache` on the slot (rather than re-reading `code[pc]`) is the
direction the target model wants: operands pre-decoded onto the dispatch
record, handlers reaching them through `cur`. A minimal variant is
`KosSlot { KosOpHandler fn; }` with handlers reading operands from the adjacent
`code[acting_pc]`; the fuller record is preferred because it is where the
eventual decode-once win comes from and it hands the handler its operand
through the same `cur` it is already given. Cache slots stay on the `KosCode`
trailing array; `cache` is still an index into it (a future optimization can
store the resolved `KosCacheSlot *` directly on the slot).

Ownership and lifecycle:

- Written at finalize, once, after emit and jump-target validation, in the
  block's writable window before the `mprotect` seal -- the same window that
  populates the `[cpool]` pointers.
- Regenerated from the `(op, arg)` stream on every load, so it is dropped by
  `soften` and never part of the `.kio` payload, the same contract the `cache`
  index already has. The `fn` values are resolved from `kos_op_handlers` at
  each finalize.
- `pack_into` relocates the whole block, slot region included; a `KosOpHandler`
  value is address-space-global and survives the memory move, as the `[cpool]`
  pointers already do.

Phase-1 `fn` values are write-once, so the read-only seal is no constraint. A
future deoptimization or soften step that must rewrite a handler pointer after
the seal would place the mutable pointers outside the read-only region, the way
the mutable `KosCacheSlot` trailing array already sits apart from `[code]`;
that is the axis that would move the slot out of the block, and it does not
apply in phase 1.

Because the slot region is indexed by the same `pc` as `[code]`, **the
index-based `pc` and the index-based jump encoding are preserved unchanged.**
Phase 1 touches neither the jump operand representation nor `es->pc` semantics.
Dispatch reaches the handler by `dispatch[pc].fn` and operands by
`dispatch[pc].arg`.

### 3.3 Finalize lowering

A new step run after emit, which is where the instruction bytes are written and
the per-site `cache` indices are assigned, so `op`/`arg`/`cache` are settled by
the time the slot region is filled:

```c
for (uint32_t pc = 0; pc < code_len; pc++) {
  KosOp o = code[pc];
  dispatch[pc].fn    = kos_op_handlers[o.op];
  dispatch[pc].op    = o.op;
  dispatch[pc].arg   = o.arg;
  dispatch[pc].cache = o.cache;
}
```

The `THEEND` slot resolves to `op_THEEND`, preserving the barrier semantics:
reaching it faults exactly as an off-the-end fetch does today.

### 3.4 The dispatch loop

With the label table gone, the loop body shrinks to a fetch, an indirect call,
and a branch on the result:

```c
for (;;) {
  KosSlot *cur = &dispatch[KosEvalState_PC(es)];
  es->acting_pc = KosEvalState_PC(es);
  es->pc = es->acting_pc + 1u;

  KosOpResult res = cur->fn(es, cur);
  if (likely(res.kind == KOS_RES_NORMAL)) {
    if (unlikely(++gil_poll_ops > KOS_EVAL_GIL_POLL)) { /* poll */ }
    continue;
  }
  /* control event: existing cg_handle_op_result / apply_kos_op_result path */
}
```

The prologue (`cg_prologue`: pending-exception, transfer-plan drain, native
iteration), the GIL/signal poll (`cg_poll`), and the result handler
(`cg_handle_op_result`, including the plain-return fast arm) carry over
essentially unchanged; only the fetch-and-jump core is replaced. The
`KOS_RES_DISPATCH` and `KOS_RES_REPROLOGUE` re-entry paths become `continue`
and a jump back to the prologue.

### 3.5 Single eval function

The sole reason `koskari_eval_dispatch_computed_goto` is a separate function is
that the label addresses and `optable` must be function-local. Removing them
removes the constraint, so the dispatch loop folds back into the body of
`koskari_eval`. The merged loop is the trampoline the target model returns to,
so building it here also builds the phase-2 return target.

### 3.6 Handler conversion

The handler signature changes from `KosOp *insn` to `KosSlot *cur`. Operand and
cache reads change from `KosOp_ARG(insn)` / `KosOp_CACHE(insn)` to
`cur->arg` / `cur->cache` (or accessor macros `KosSlot_ARG` / `KosSlot_CACHE`
to keep the sweep mechanical and reversible). This is a wide but mechanical
edit across every file in `opcode/`. Jump handlers keep writing `es->pc`
(see §4).

---

## 4. The Dispatch Macro Seam

Handlers today end their normal path with `return EVAL_NORMAL();` and their
branch path with `KosEvalState_SET_PC(es, target); return EVAL_NORMAL();`.
Phase 1 routes both through macros so the tail-call flip is a macro
redefinition rather than another sweep:

```c
/* Phase 1 (loop) definitions: */
#define EVAL_DISPATCH_NORMAL(es, cur)     return EVAL_NORMAL()
#define EVAL_DISPATCH_TO(es, cur, target) \
    do { KosEvalState_SET_PC((es), (target)); return EVAL_NORMAL(); } while (0)
```

`EVAL_DISPATCH_NORMAL` is the sequential-successor exit (loop advances via
`es->pc = acting_pc + 1`). `EVAL_DISPATCH_TO` is the branch exit. Both keep the
convention that the successor `pc` is materialized on `es` before the macro
fires, so both dispatch forms can find it. Every handler's normal and branch
exits are converted to these macros in phase 1, where they still expand to a
plain return.

The phase-2 definitions (§6) redefine the same macros to `musttail` to the
successor slot's handler; no handler body changes again.

---

## 5. Performance and Portability Expectations

Phase 1 is expected to be **perf-flat to slightly negative**, and should not
be gated on a speedup. A computed goto is one indirect branch with all state in
loop-local registers. Phase 1 replaces it with an indirect call, a return, the
loop's post-call result branch, and marshaling `es`/`cur` into argument
registers per call. Because the call clobbers caller-saved registers, the loop
cannot keep facets in caller-saved registers across iterations. The previously
always-inlined hot handlers (`CONST`, `POP`, `FAST_*`) now pay a real call.
This tax is accepted: its value is structural (it builds the slot array, the
extern table, the merged trampoline, and the macro seam), and phase 2 removes
precisely this tax -- the tail call passes arguments in place with no
survive-across-call requirement and collapses call+return into a jump. The
trajectory is monotonic; do not judge phase 1 by `churn`/`binary_trees`
numbers.

Portability improves in phase 1: fn-pointer dispatch is standard C, so the base
build no longer depends on computed goto. Phase 2 reintroduces a musttail
dependency; see §6.2 for the compiler mandate that takes effect once phase 2
implementation begins.

---

## 6. Phase 2 — Tail-Call Threading (scoped, not committed here)

Deferred. Recorded so phase 1 does not foreclose it.

- Redefine `EVAL_DISPATCH_NORMAL` / `EVAL_DISPATCH_TO` to
  `KOS_MUSTTAIL return next->fn(es, next, ...)` where `next` is `cur + 1` or
  the branch target slot.
- Gate on a `KOS_HAVE_MUSTTAIL` feature test during phase-2 bring-up, with the
  phase-1 loop body as the transitional fallback while the tail-call form is
  landed and verified. §6.2 covers what ships once it is.
- Enforce the §2 invariant: audit every control-event exit to confirm promoted
  facets are flushed to `KosEvalState` before returning to the trampoline.
- Safety-net stack walks (`KOS_EVAL_SAFETYNET`) can only run at trampoline
  returns under threading, since mid-run frames are not reachable. Coarser
  granularity than today; note in the safetynet docs when phase 2 lands.

### 6.1 Signature and the register-budget decision

The base signature is two pointers, `disp fn(KosEvalState *ev, KosSlot *cur)`,
and phase 1 ships exactly this. Rationale for not widening:

- Two pointers never overflow any target ABI's argument registers, so phase 1
  handlers stay leaf-cheap by construction and the same signature survives the
  `musttail` flip unchanged.
- `ev` is pinned in a callee-saved register across the loop, so per-call
  marshaling is effectively just `cur`.
- Segment and namespace are the two most-touched facets, but they are **not**
  promoted to arguments. In the loop model a handler that mutates the current
  segment (a push spilling to a new segment) or the namespace (entering a
  scope) has to propagate that change back through `ev` anyway -- the loop is
  the join point and C cannot cheaply return several values -- so passing them
  as arguments buys nothing on the write path and forces the caller to load
  them from `ev` on every call whether or not the handler uses them. Handlers
  that need seg/ns load them from `ev` at entry, where the compiler keeps the
  loaded value in a register for the handler's duration.

Promoting seg/ns to a four-argument form
(`fn(ev, cur, KosSegment *seg, KosNamespace *ns)`) pays only under `musttail`,
where there is no loop join: the tail-call arguments are the state, flowing
call to call, and a mutation is just passing a different value to the next
handler with writeback to `ev` deferred to control events. If phase 2
measurement shows seg/ns reloads dominating, the promotion is introduced there
and only there, again as a macro redefinition of the dispatch seam.

### 6.2 Compiler mandate (decided)

Once phase 2 implementation begins, musttail support is required, not
feature-gated: clang, or GCC 15+. The phase-1 loop stops being a permanent
fallback backend and becomes bring-up scaffolding only, per §5. This
supersedes the "build-gated dispatch backend" framing in §2.

This decision takes effect when phase 2 work starts, not before; phase 1
and the current portable build stay as-is until then.

Consequence for release engineering, to land alongside phase 2 rather than
after: build scripts and the PyPI prebuilt wheel matrix must pin a
musttail-capable compiler on every platform the wheel matrix covers. A
platform whose toolchain cannot reach clang or GCC 15+ drops out of the
prebuilt matrix rather than falling back to phase-1 dispatch at run time.

---

## 7. Change Inventory (phase 1)

- **hints.h / new dispatch header:** `KosOpHandler` typedef; `extern` decl of
  `kos_op_handlers`; retire or repurpose `KOS_USE_COMPUTED_GOTO`.
- **eval.c:** drop `always_inline` from `KOS_OP_HANDLER`; emit
  `kos_op_handlers[]` from `opcodes.incl`; replace
  `koskari_eval_dispatch_computed_goto` with the fn-pointer loop; fold it into
  `koskari_eval`; define `EVAL_DISPATCH_NORMAL` / `EVAL_DISPATCH_TO` as
  returns; delete the label-table `OP` expansion and `KOS_EVAL_PREPARE_NEXT_INSN`
  computed-goto form (keep the fetch logic in the loop).
- **eval.h:** `KosSlot` struct; accessor macros (`KosSlot_ARG`,
  `KosSlot_CACHE`).
- **bytecode.h:** `KosSlot *dispatch;` field on `KosCode` pointing at the new
  block region; accessor.
- **bytecode.c:** add the slot region to `code_block_layout`; populate
  `dispatch` in the finalize lowering step within the block's writable window;
  confirm `pack_into` recomputes the region's offset alongside the others;
  confirm `soften` ignores it.
- **opcode/*.c:** signature `KosOp *insn` -> `KosSlot *cur`; operand/cache reads
  via `cur`; normal/branch exits via the dispatch macros.
- **opcodes.incl:** unchanged (still the single source for enum + table);
  incoming opcodes keep their current `(op, arg)` representation.

The incoming opcode stream, `soften`, and `.kio` are all unchanged: they
round-trip through the `(op, arg)` pairs, and finalize re-expands the slot
region from them on every load. The slot region is the in-memory dispatch
lowering of that same bytecode.
