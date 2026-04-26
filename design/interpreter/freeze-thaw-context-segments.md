# Koskari Design Note: Freeze/Thaw Context Segments and C-Local Execution State

## Purpose

This document summarizes the design direction for improving Koskari's interpreter hot path by:

1. Splitting persistent heap context from active execution state.
2. Introducing explicit **freeze/thaw boundaries** between them.
3. Moving frequently mutated execution state into **C-local structures** during evaluation.
4. Adding a **fast inline stack block** used during active evaluation.
5. Removing **copy-on-write (CoW) stack blocks** in favor of explicit snapshotting (see `design/archive/stack-no-cow-proposal.md`).

The primary goal is to reduce indirection and conditional checks in the hot opcode path while preserving the ability to suspend, resume, and capture execution state.

---

# 1. Architectural Overview

Koskari evaluation naturally divides runtime state into two categories:

| Category                   | Lifetime                            | Storage                  |
| -------------------------- | ----------------------------------- | ------------------------ |
| Persistent execution state | Survives suspension/resume          | Heap (`KosContext`)      |
| Active execution state     | Exists only during `koskari_eval()` | C-local (`KosEvalState`) |

The interpreter therefore operates using a **freeze/thaw model**:

```
Heap Context
   │
   │ thaw
   ▼
C-local EvalState (fast execution representation)
   │
   │ freeze
   ▼
Heap Context
```

The heap context acts as the **persistent serialized form of execution**, while the C-local state acts as the **accelerated working representation**.

---

# 2. Persistent Context (Heap)

`KosContext` remains heap-allocated and persists across evaluator drives.

It owns state required for:

* coroutine / generator suspension
* async await suspension
* continuation capture
* transfer plan scheduling
* wrapper lifecycle

Examples of persistent elements:

* dynchain head
* frame chain
* suspension metadata
* transfer plans
* resume tokens
* frozen stack representation

Importantly, persistent context **must never reference C-local memory**.

---

# 3. Resume Image (Freeze/Thaw Segment)

Within `KosContext`, a dedicated sub-structure represents the **execution image required to resume evaluation**.

Conceptually:

```
KosContext
   ├─ runtime control state
   └─ resume_state
        ├─ current frame
        ├─ program counter
        ├─ active stack block (heap form)
        └─ other resume-critical fields
```

This structure represents the minimal persistent information required to reconstruct the active evaluator state.

This segment is the **boundary object between heap and C-local execution state**.

---

# 4. C-Local EvalState

During active interpretation, `koskari_eval()` constructs a C-local execution state derived from the resume image.

Example conceptual structure:

```
typedef struct KosEvalState {
    KosContext *ctx;

    /* cached execution state */
    KosFrame   *frame;
    KosCode    *code;
    uint32_t    pc;

    /* accelerated stack */
    KosStackExec stack;

    /* inline stack block */
    KosStackBlock local_block;
    PyObject *local_objs[FAST_STACK_SIZE];

} KosEvalState;
```

This state exists **only during active execution**.

Opcode handlers operate exclusively on this structure.

Persistent context is updated only during freeze operations.

---

# 5. Fast Inline Stack Block

A key performance feature of the design is an **inline stack block stored in C-local memory**.

Characteristics:

* Fixed small capacity (example: 32 entries).
* Stored inside `KosEvalState`.
* Used as the active block for most stack operations.

Advantages:

* avoids heap allocation in the hot path
* reduces pointer indirection
* improves cache locality

The active stack pointer may reference either:

* the inline block
* a heap block

**Segmented stack natural fit**: KosStackBlock is already a linked list of blocks. Freeze copies one block (the current one) to heap; thaw copies one block from heap into the cursor. The block boundary is the natural copy unit—no new segmentation concept is required.

**Block nesting (PUSH_LOOP, PUSH_TRY, etc.)**: PushBlock and DropBlock are rarer than plain push/pop. When we PushBlock we spill the inline block to heap, allocate a child block, and either use a fresh inline block or point at the heap block. Most stack ops operate on a single block and never cross block boundaries, so the single-block fast path dominates.

---

# 6. Stack Block Representation

Stack blocks retain their segmented structure:

```
struct stackblock {
    struct stackblock *parent;
    uint refcount;
    uint max;
    uint len;
    PyObject *objs[];
}
```

However:

* **Copy-on-write is removed.**
* Blocks are assumed uniquely owned during execution.
* Continuations use explicit cloning instead.

This removes a conditional check from every stack write.

---

# 7. Stack Transitions

### Local → Heap (overflow)

If the inline block exceeds capacity:

1. allocate or reuse heap block
2. copy inline contents
3. switch active block pointer to heap block

### Push Block (extent entry)

When creating a child stack block:

1. allocate heap block
2. copy current block into heap block
3. reset inline block
4. set parent pointer

### Pop Block

When removing a block:

1. clear current block
2. inspect parent
3. if parent small → copy into inline block
4. otherwise activate heap block

---

# 8. Freeze Operation

Before any state escape, the active state must be committed to heap form.

This is handled by:

```
ensure_heap_block()
```

Responsibilities:

* detect if active block is inline
* allocate heap block if needed
* copy stack contents
* update persistent context

Freeze must occur before:

* await suspension
* yield suspension
* continuation capture
* iterator freezing
* leaving `koskari_eval()` with resumable state

**Scope: suspension vs. frame switch**: The critical design choice is *when* freeze/thaw runs. If freeze/thaw is scoped **only to suspension boundaries** (eval entry/exit, AWAIT, YIELD, RETURN, RAISE), then thaw happens once per `koskari_eval` call and freeze once per suspend. A typical run of 10,000+ instructions amortizes the copy cost over many stack ops—a clear win. If freeze/thaw runs on **every frame switch** (CALL/RETURN), we do 2 block copies per function call. For call-heavy code, that frequency could erode gains. The recommended approach: scope freeze/thaw to suspension only. On CALL we must persist the caller’s stack to its frame; the callee’s stack may start empty and stay in the cursor. The complexity is ensuring the caller’s frame holds a valid heap block before we run the callee—which we already need for correct suspension anyway.

---

# 9. Thaw Operation

When evaluation resumes, the runtime reconstructs active state.

This may optionally accelerate the stack:

```
accelerate_block()
```

If the heap block is small enough:

* copy into inline block
* release heap block
* activate inline stack

Otherwise heap stack remains active.

Acceleration is optional and purely a performance optimization.

---

# 10. Opcode Interface Changes

Opcodes now operate on **eval-local execution state**, not on the heap context.

Typical signature:

```
opcode_handler(KosEvalState *es, ...)
```

Stack operations become helpers:

```
kos_es_push(es, value)
kos_es_pop(es)
kos_es_push_block(es)
kos_es_pop_block(es)
```

Opcodes no longer access:

```
ctx->frame->stack
```

This eliminates repeated pointer traversal.

---

# 11. Removal of Stack CoW

The previous design used copy-on-write blocks to optimize continuation capture.

Problems:

* every stack write required a CoW check
* hot path polluted by continuation logic

New approach:

* stack blocks are uniquely owned
* continuation capture clones blocks explicitly

This moves cost from:

* every stack mutation

To:

* continuation capture

Which is much rarer.

---

# 12. Design Goals

Primary goals:

* reduce stack mutation overhead
* eliminate unnecessary pointer indirection
* improve cache locality
* isolate suspension logic from hot opcode paths
* maintain clear ownership semantics

Secondary goals:

* make opcode implementations simpler
* centralize stack transition logic
* make continuation semantics explicit

---

# 13. Key Invariants

1. C-local execution state never escapes `koskari_eval()`.
2. Heap context never references C-local memory.
3. Freeze operations commit execution state to heap.
4. Thaw operations reconstruct active execution state.
5. Stack blocks are uniquely owned during execution.

These invariants maintain correctness while enabling aggressive hot-path optimizations.

---

# 14. Expected Performance Impact

Improvements expected in:

* push/pop heavy opcodes
* arithmetic/object operations
* stack shuffling instructions

Reasons:

* fewer pointer dereferences
* removal of CoW checks
* fewer heap allocations
* better CPU cache locality

The interpreter hot path becomes dominated by:

```
objs[len++] = value
value = objs[--len]
```

with minimal surrounding overhead.

---

# 15. Future Optimization Opportunities

Possible future extensions:

* register caching of stack pointers
* specialized fast opcode variants
* block size tier freelists
* improved DECREF clearing strategies
* inline unions on `KosContext` for state-specific fields (suspension payload, delimited-return path) to clarify invariants and reduce effective size when only a subset of fields is active

These can be explored after measuring the new architecture.

---

# 16. Freeze/Thaw Cost-Benefit Analysis

A natural concern: **does the overhead of freeze/thaw exceed the savings?**

### Cost of freeze/thaw

Each block copy is roughly:

* 8 slots → 64 bytes
* 16 slots → 128 bytes

On a modern CPU with warm caches, that is on the order of ~10–30 cycles per `memcpy`. Two copies (freeze + thaw) per suspension is ~20–60 cycles.

### Benefit of the inline cursor

Per stack operation, we eliminate:

* function call overhead (`KosStackBlock_PUSH` → direct store)
* indirection through `frame->stack->objs[i]`
* CoW check (after removal)
* grow check (until inline capacity is reached)

Roughly 10–20 cycles saved per stack op. With 2–3 stack ops per instruction average, that is ~20–60 cycles saved per instruction.

### When it pays off

* **Long runs between suspends** (e.g. 10,000 instructions): 1 thaw + 1 freeze. Copy cost ~50 cycles vs. savings ~200,000–600,000 cycles. Net win is large.
* **Call-heavy code**: If freeze/thaw is scoped to suspension only (section 8), we avoid per-call copies. If we instead froze/thawed on every CALL, we might spend ~40 cycles per call; with 50 instructions per call we would still save ~1,000–3,000 cycles per call. The margin narrows for very shallow call depths.

### Mitigation if cost is a concern

1. **Scope freeze/thaw to suspension only** — avoid per-frame copies.
2. **Measure first** — add instrumentation to count freeze/thaw frequency and cycles before and after.
3. **Skip thaw when stack does not fit** — if the heap block is larger than inline capacity, leave it on the heap; no copy.
4. **Implement in phases** — remove CoW first and measure. Add the cursor and measure again. If the cursor does not show clear improvement, we can stop at no-CoW.

### Recommended order

1. Remove CoW (see `design/archive/stack-no-cow-proposal.md`). Simpler, independent win; no freeze/thaw yet.
2. Measure.
3. Introduce `KosEvalState` and inline stack. Measure again.
4. Tune (inline size, thaw heuristics) based on results.

---

# Summary

This design introduces a **freeze/thaw execution model** separating persistent runtime state from accelerated C-local execution state.

Combined with removal of stack CoW (see `design/archive/stack-no-cow-proposal.md`) and the introduction of a **fast inline stack block**, this architecture significantly simplifies the interpreter hot path while preserving Koskari's advanced control-flow capabilities.

Section 16 addresses the concern that freeze/thaw overhead may exceed savings; the analysis concludes that with suspension-scoped freeze/thaw and typical instruction counts, the net win is large. Mitigations are documented if measurement suggests otherwise.

---

# Addendum: Codebase Findings, Sketch of Changes, and Implementation Plan

*Addendum added after codebase exploration. Documents current-state findings, proposed change sketch, and phased implementation plan.*

---

## A. Current-State Findings

### A.1 KosContext and Execution Flow

**Location**: `koskari/lib/eval.h`, `eval.c`

`KosContext` is a heap-allocated struct holding persistent VM state: dynchain, frame cache, **`block`** (pointer to the current operand-stack `KosStackBlock *`), transfer plans, Python bridge (await/yield), exception injection, suspension bookkeeping, and flags. Delimited invoke return metadata lives on **`KOS_DYN_DELIM`** nodes, not in extra context fields.

`koskari_eval()` drives the interpreter. Opcodes receive **`(KosEvalState *es, KosEvent *out_ev, KosOp *insn)`** and use **`KosEvalState_*`** stack helpers and/or `KosContext_BLOCK(ctx)` after cache refresh. Eval-local macros in `eval.c` still map `CURRENT_FRAME(ctx)` → `frame_top`, `CURRENT_CODE` / `CURRENT_PC` from that frame; the operand stack is the block referenced from **`ctx->block`**, which tracks **`KosDynNode.stack`** on installed nodes (see [node-owned-operand-stack.md](../node-owned-operand-stack.md)).

### A.2 Stack and CoW

**Location**: `koskari/lib/stack.c`, `stack.h`

`KosStackBlock` has `back` (parent), `refcount`, `max`, `len`, and `objs[]`. `KosStackBlock_EnsureExclusive()` is called on every mutating operation; it checks `refcount == 1` and, if not, calls `KosStackBlock_CopyForWrite()`.

**CoW call sites** (in `stack.c`): `Push`, `Pop`, `PushRef`, `PUSH`, `PopRef`, `Drop`, `Drop1`, `DropNoErr`, `PopTail`, `PopTailPtr`, `PopTailDict`, `PopTailDictNames`, `Replace`, `Duplicate`, `ClearBlock`, `PushBlock`, `PopBlock`, `DropBlock`, and several PyObject-facing `KosStack_*` wrappers.

**Continuation capture** uses `KosStackBlock_COW()` (increment refcounts on the chain) rather than `KosStackBlock_DeepCopy()`. Call sites: `kos_dynframe_clone`, transfer-plan resume-stack handling, and several places in `dynchain.c` (e.g., `kos_dyn_install`, `kos_build_return_plan(es, …)`, `handle_continuation_apply`).

### A.3 Opcode Dispatch

**Location**: `koskari/lib/eval.c` (phases 4–6), `opcodes.incl`

Opcodes are dispatched via computed goto or switch. Each handler has signature:

```c
KosOpOutcome op_<name>(KosEvalState *es, KosEvent *out_ev, KosOp *insn);
```

`KOS_EVAL_PREPARE_NEXT_INSN` copies the current instruction into a local `KosOp insn` before each dispatch; **`op_*`** still take `KosOp *` and receive **`&insn`**. Post-opcode handling does not use a separate dispatch struct. Opcodes use `KosEvalState_Context(es)` where they need `ctx`, and **`KosEvalState_Stack*`** / related helpers for operand-stack access.

### A.4 DynChain and Frames

**Location**: `koskari/lib/dynchain.h`, `dynchain.c`

`KosDynFrame` (a `KosDynNode` variant) holds `code`, `pc`, `acting_pc`, `ns` in `u.frame`. Operand stack storage for a node is **`KosDynNode.stack`** (macro `KosDynFrame_STACK` reads the node base field). Frames are pushed/popped via `kos_dyn_push_frame`, `kos_dyn_pop_frame`; chain structure drives prompts, wind, try/finally, loop/foreach, WITH, DELIM, etc.

**Block transitions** (push/pop block) occur in: `PUSH_TRY`, `PUSH_FINALLY`, `PUSH_LOOP`, `PUSH_FOREACH`, `PUSH_COLLECT`, `PUSH_PROMPT` (via `kos_dyn_install`), `PUSH_WITH`/`PUSH_DYN_WITH`, and their corresponding pop/drop opcodes.

### A.5 Suspension and Resume Points

State escapes at: AWAIT (pending `py_await_iter`), YIELD (gen boundary), YIELD_FROM (delegate stepping), WITH async enter/exit, transfer-plan await, and continuation capture (PUSH_CC, INVOKE_K, POP_PROMPT, transfer plans). All of these require heap-resident stack before leaving `koskari_eval()`.

---

## B. Sketch of Changes

### B.1 Introduce KosEvalState

**Implemented** `KosEvalState` in `eval.h` holds `ctx`, `frame`, and `code`; stack
access goes through **`KosEvalState_Stack*`** against **`KosContext_BLOCK`**.
The larger **inline-stack** sketch below was exploratory and is **not** the
current layout:

```c
#define KOS_FAST_STACK_SIZE 32

typedef struct KosEvalState {
  KosContext *ctx;
  KosDynFrame *frame;
  KosCode *code;
  uint32_t pc;

  /* Active stack: either inline or heap */
  int using_inline;           /* 1 = local_objs, 0 = heap_block */
  KosStackBlock *heap_block;  /* when using heap */
  PyObject *local_objs[KOS_FAST_STACK_SIZE];
  size_t stack_len;
  size_t stack_max;           /* KOS_FAST_STACK_SIZE or heap_block->max */
} KosEvalState;
```

`koskari_eval()` allocates `KosEvalState es` on the stack (or as a local struct), populates it from `ctx` (thaw), and passes `&es` to the dispatch/opcode layer.

### B.2 Thaw at Entry

On `koskari_eval` entry (and after revive paths):

- Set `es.ctx = ctx`, `es.frame = ctx->frame_top`, `es.code`, `es.pc` from frame.
- If frame has a stack block and it is small enough: copy into `local_objs`, set `using_inline = 1`.
- Else: set `using_inline = 0`, `heap_block = frame->stack`.

### B.3 Freeze at Suspension

Before any suspension path, call `ensure_heap_block(&es)`:

- If `using_inline`, allocate heap block, copy `local_objs[0..stack_len-1]`, update `frame->stack` (or the active frame’s stack slot).
- Ensure `ctx` / frame chain hold no pointers to `es.local_objs`.

### B.4 Stack Helpers

Introduce `kos_es_push`, `kos_es_pop`, `kos_es_peek`, `kos_es_drop`, etc., operating on `KosEvalState *`. These:

- Use `local_objs` when `using_inline` and capacity allows.
- On overflow: call `ensure_heap_block`, then push on heap.
- Never call `KosStackBlock_EnsureExclusive` or `CopyForWrite`.

### B.5 Opcode Signature Change

Migrate opcodes from:

```c
int op_X(KosContext *ctx, KosEvent *out_ev, KosOp *insn);
```

to:

```c
int op_X(KosEvalState *es, KosEvent *out_ev, KosOp *insn);
```

`ctx` remains available as `es->ctx`. Replace `CURRENT_STACK(ctx)` with `kos_es_*` calls. `CURRENT_FRAME`, `CURRENT_CODE`, `CURRENT_PC` become `es->frame`, `es->code`, `es->pc` (or macros).

### B.6 Remove CoW from Hot Path

- Remove `KosStackBlock_EnsureExclusive` from all stack mutation paths used during normal evaluation.
- Change continuation capture to use `KosStackBlock_DeepCopy` (or equivalent explicit clone) instead of `KosStackBlock_COW`. Update `kos_dynframe_clone` and all dynchain/transfer-plan sites that currently use COW.
- Retire or repurpose `KosStackBlock_COW` and `KosStackBlock_CopyForWrite` for the new model (or remove once no longer used).

### B.7 Block Push/Pop

For PUSH_LOOP, PUSH_TRY, etc.:

- `kos_es_push_block(es)`: freeze current block to heap if inline, allocate child block, set `es` to use it (either as new inline or heap).
- `kos_es_pop_block(es, count)`: pop block, optionally copy parent back into inline if small.

---

## C. Proposed Implementation Plan

**Order of optimizations**: Remove CoW first (Phases 1 and 3). Measure. Add the cursor and inline stack (Phases 2 and 4). Measure again. This isolates the contribution of each change and avoids sinking effort into the cursor if no-CoW alone achieves the goal.

### Phase 1: Prepare (Low Risk)

1. **Audit CoW usage**: Enumerate all `KosStackBlock_COW` and `KosStackBlock_EnsureExclusive` call sites; document which serve continuation capture vs. normal eval.
2. **Extract resume image**: Identify the minimal set of `KosContext` + frame fields needed to reconstruct execution. Optionally add a `KosResumeImage` sub-structure (or comments) to clarify the boundary.
3. **Add `KosStackBlock_DeepCopy` usage for continuations**: Change `kos_dynframe_clone` and related continuation/transfer-plan paths to use `DeepCopy` instead of `COW`. Verify tests. This does not remove CoW from the hot path yet but aligns continuation semantics with the design.

### Phase 2: Introduce KosEvalState (No Inline Stack Yet)

1. Define `KosEvalState` with `ctx`, `frame`, `code`, `pc`, and a single `heap_block` pointer (no inline block yet).
2. At `koskari_eval` entry, populate `es` from `ctx` and pass `&es` through dispatch.
3. Change opcode signatures to `(KosEvalState *es, KosEvent *out_ev, KosOp *insn)` and replace `CURRENT_*` with `es->*` access. Keep using existing `KosStackBlock_*` APIs via `es->heap_block` (or `KosDynFrame_STACK(es->frame)` temporarily).
4. Ensure all opcodes and dynchain helpers that touch the stack during eval go through `es`. Update `eval.c` phase helpers and any shared code.

### Phase 3: Remove CoW from Eval Path

1. Assert that during active eval, the current frame’s stack block is uniquely owned (no other continuation or snapshot holds it).
2. Remove `KosStackBlock_EnsureExclusive` from all stack mutation paths used by the evaluator. Leave it only for legacy or non-eval paths if any remain.
3. Run full test suite; fix any regressions.

### Phase 4: Inline Stack Block

1. Add `using_inline`, `local_objs[KOS_FAST_STACK_SIZE]`, `stack_len` to `KosEvalState`.
2. Implement thaw logic: when resuming, if the frame’s heap block is small enough, copy into `local_objs` and set `using_inline = 1`.
3. Implement `ensure_heap_block(es)` and call it before every suspension path.
4. Implement `kos_es_push`, `kos_es_pop`, etc., to use inline block when possible.
5. Migrate opcodes from `KosStackBlock_*` to `kos_es_*` gradually; ensure overflow and block push/pop are correct.

### Phase 5: Block Transitions and Cleanup

1. Implement `kos_es_push_block` / `kos_es_pop_block` with inline-awareness.
2. Remove or deprecate `KosStackBlock_COW` and `KosStackBlock_CopyForWrite` if unused.
3. Performance measurement and tuning (inline size, thaw acceleration heuristics).

---

## D. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Subtle continuation/transfer bugs when switching COW→DeepCopy | Phase 1: comprehensive tests before removing CoW; consider temporary debug asserts. |
| Missing freeze before a suspension path | Audit all suspension branches; centralize freeze in a single “prepare to suspend” helper. |
| Regressions in multi-frame or nested block scenarios | Add targeted tests for loop/foreach/try/finally, PUSH_CC, transfer plans. |
| Increase in code size or complexity | Keep `KosEvalState` and helpers in a focused set of files; maintain clear freeze/thaw comments. |
| Freeze/thaw overhead exceeds savings | Scope freeze/thaw to suspension only (not per CALL); measure before and after; skip thaw when stack exceeds inline capacity; implement no-CoW first and validate that win before adding cursor. |

---

## E. Files Requiring Modification (Preliminary)

- `koskari/lib/eval.h`, `eval.c` — `KosEvalState`, thaw/freeze, dispatch changes
- `koskari/lib/stack.c`, `stack.h` — CoW removal, possibly new `kos_es_*` helpers (or in eval.c)
- `koskari/lib/dynchain.c` — continuation/transfer COW→DeepCopy, any stack access during eval
- `koskari/lib/opcode/*.c` — all opcodes (signature and stack access)
- `koskari/lib/object.c` — `CURRENT_*` usage if any
- `design/interpreter/evaluator.md` — update to describe the new model

---

*End of addendum.*
