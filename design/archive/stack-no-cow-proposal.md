# Design Proposal: Remove Copy-on-Write Semantics from KosStackBlock

**Status:** Draft
**Author:** Design proposal
**Date:** 2025-03-10

## Summary

Remove copy-on-write (COW) semantics from the stack block APIs. Blocks become exclusively owned; no sharing. All continuations are multi-shot: each invocation clones the dynchain and its stack as a full DeepCopy (with PyObject INCREFs), and the installed copy may be mutated. The original continuation capture remains frozen. Only one owner per stack block chain; no refcount needed. The hot path for value stack operations drops the per-write refcount check (`EnsureExclusive`).

## Motivation

**Current tradeoff:**
- **COW benefit:** On capture, we avoid touching every stack value—only bump block refcounts (O(blocks)).
- **COW cost:** Every push/pop/extend/modify goes through `EnsureExclusive`, which checks `refcount == 1` and, when shared, runs `CopyForWrite` (full copy + incref per value).

**Proposed tradeoff:**
- **Benefit:** Hot path has no refcount check; simpler, faster per-operation.
- **Cost:** Capture pays O(values) once to deep-copy the stack and incref values.

For typical stacks (4–32 slots per block), the per-write overhead may dominate. Dropping COW simplifies the model and prepares for future linear-array stack designs.

---

## Current Architecture

### KosStackBlock Structure

```c
struct KosStackBlock {
  KosStackBlock *back;    /* previous block (toward bottom) */
  size_t max;
  size_t len;
  PyObject *objs[0];
};
```

After this change, the `refcount` field is removed. Each block chain has a single owner.

### COW-Related APIs

| API | Purpose | Callers |
|-----|---------|---------|
| `KosStackBlock_EnsureExclusive(KosStackBlock **block)` | Internal: if refcount > 1, copy block and decref old | All mutating stack ops (18 call sites in stack.c) |
| `KosStackBlock_CopyForWrite(KosStackBlock **block)` | Implements EnsureExclusive | EnsureExclusive only |
| `KosStackBlock_COW(KosStackBlock *block)` | Bump refcount on all blocks in chain; return same chain | dynchain.c (7 call sites) |
| `KosStackBlock_DeepCopy(KosStackBlock *block)` | Allocate new blocks, copy objs, incref each | Currently unused except as implementation detail |
| `KosStackBlock_Release(KosStackBlock *block)` | Decrement refcount; free if 0, else DECREF objs | Many |
| `KosStackBlock_ReleaseExclude(block, exclude)` | Same as Release but skip DECREF on slots == exclude | cont.c, dynchain.c (re-entrancy guard) |
| `KosStackBlock_ReleaseChainExclude(block, exclude)` | Release full chain with exclude | dynchain.c, cont.c |
| `KosStack_Copy(PyObject *original)` | COW copy of KosStack: bump block refcounts | stack.c `stack_m_copy`, tests |

### EnsureExclusive Call Sites (stack.c)

All mutating operations call `EnsureExclusive` before modifying:

- `KosStackBlock_Push` (line 360)
- `KosStackBlock_PUSH` (line 394)
- `KosStackBlock_ExtendPtr` (line 427)
- `KosStackBlock_EXTENDPTR` (line 471)
- `KosStackBlock_Pull` (line 555)
- `KosStackBlock_Pop` (line 593)
- `KosStackBlock_Drop` (line 619)
- `KosStackBlock_Drop1` (line 645)
- `KosStackBlock_Duplicate` (line 674)
- `KosStackBlock_Replace` (line 703)
- `KosStackBlock_PopTail` (line 741)
- `KosStackBlock_PopBlock` (line 779, for `back` block)
- `KosStackBlock_PopTailPtr` (line 925)
- `KosStackBlock_PopTailDict` (line 959)
- `KosStackBlock_PopTailDictNames` (line 1078)
- `KosStackBlock_ClearBlock` (line 205)
- `KosStack_PopBlock` (line 1811, for `back` block)

### COW Usage in Dynchain (dynchain.c)

| Location | Usage |
|----------|-------|
| `kos_dynframe_clone` (180) | `cloned_stack = KosStackBlock_COW(KosDynFrame_STACK(frame))` |
| `kos_shift_capture` (2542) | `resume_stack = KosStackBlock_COW(KosDynFrame_STACK(resume_frame))` |
| `kos_capture_cc` (2618) | `KosStackBlock_COW(KosDynFrame_STACK(resume_frame))` |
| `kos_continuation_clone` (2705) | `KosStackBlock_COW(KosContinuation_RESUME_STACK(k))` |
| `xfer_step_install` (3545) | `new_stack = KosStackBlock_COW(plan->resume_stack)` |
| `xfer_step_push` (3660) | Same |
| `kos_build_continuation_invoke_plan` (4081) | `plan->resume_stack = KosStackBlock_COW(to_use->resume_stack)` |

### Multi-Shot Continuations and Ownership

**All continuations are multi-shot.** The original capture stays frozen. Each invocation gets a fresh full copy of the dynchain and stack; the installed copy may be mutated. Only one owner per stack block chain at any time—no refcount needed.

### Frame Cloning and Continuation Flow

1. **kos_dynframe_clone** (used by kos_dyn_clone for KOS_DYN_FRAME)
   - Called from `kos_clone_chain_segment` when cloning a frame node.
   - Currently: `cloned_stack = KosStackBlock_COW(frame->stack)`.
   - Proposed: `cloned_stack = KosStackBlock_DeepCopy(frame->stack)`.

2. **Continuation capture** (shift, capture-cc)
   - Captures chain via `kos_clone_chain_segment`; each frame gets DeepCopy of its stack (from `kos_dynframe_clone`).
   - `resume_stack = KosDynFrame_STACK(resume_frame)` (the cloned chain's resume frame; same ownership).
   - Continuation owns the chain and its stacks. The captured state remains frozen.

3. **Invoke** (always multi-shot)
   - DeepCopy the continuation's `resume_stack` (and the chain, per existing kos_clone_chain_segment).
   - Install the DeepCopy on the frame. The installed copy is the sole owner and may be mutated.
   - The continuation retains its original copy for the next invoke.

---

## Proposed Changes

### 1. Stack Block Structure and Internal APIs

**Remove:**
- `KosStackBlock_EnsureExclusive`
- `KosStackBlock_CopyForWrite`
- `KosStackBlock_COW`
- `refcount` field from `struct KosStackBlock`

**Simplify:**
- `KosStackBlock_Release` / `KosStackBlock_ReleaseExclude`: always treat block as exclusively owned; directly DECREF objs and free (or cache). No refcount check.
- `KosStackBlock_ReleaseChainExclude`: keep; still needed for exclude (re-entrancy when continuation wrapper is on stack).

**Keep and use:**
- `KosStackBlock_DeepCopy`: becomes the primary "copy stack for capture" API.

### 2. Stack Mutating Operations (stack.c)

Remove all `EnsureExclusive` calls from:

- `KosStackBlock_Push`, `KosStackBlock_PUSH`
- `KosStackBlock_ExtendPtr`, `KosStackBlock_EXTENDPTR`
- `KosStackBlock_Pull`, `KosStackBlock_Pop`, `KosStackBlock_Drop`, `KosStackBlock_Drop1`
- `KosStackBlock_Duplicate`, `KosStackBlock_Replace`
- `KosStackBlock_PopTail`, `KosStackBlock_PopBlock`, `KosStackBlock_PopTailPtr`
- `KosStackBlock_PopTailDict`, `KosStackBlock_PopTailDictNames`
- `KosStackBlock_ClearBlock`
- `KosStack_PopBlock`

Each of these becomes a direct mutate with no prior check.

### 3. Frame Cloning (dynchain.c)

**kos_dynframe_clone (line 176):**

```c
// Before
KosStackBlock *cloned_stack = KosStackBlock_COW(KosDynFrame_STACK(frame));

// After
KosStackBlock *cloned_stack = KosStackBlock_DeepCopy(KosDynFrame_STACK(frame));
```

Handle NULL: if `frame->stack` is NULL, `cloned_stack` stays NULL.

### 4. Continuation Capture (dynchain.c)

**kos_shift_capture (2542), kos_capture_cc (2618):**

```c
// Before
resume_stack = KosStackBlock_COW(KosDynFrame_STACK(resume_frame));

// After
resume_stack = KosDynFrame_STACK(resume_frame);
```

The `resume_frame` is from the cloned chain. That chain's frames already have DeepCopy stacks (from `kos_dynframe_clone` in `kos_clone_chain_segment`). The continuation owns the chain; `resume_stack` is simply the resume frame's stack. One DeepCopy at capture, in the chain clone.

### 5. Continuation Clone (dynchain.c)

**kos_continuation_clone (2705):**

Currently: `KosStackBlock_COW(KosContinuation_RESUME_STACK(k))`.

Proposed: `KosStackBlock_DeepCopy(KosContinuation_RESUME_STACK(k))`. The clone gets its own copy.

### 6. Transfer Plan (dynchain.c)

**kos_build_continuation_invoke_plan (4081):**

```c
// Before
plan->resume_stack = KosStackBlock_COW(to_use->resume_stack);

// After (always multi-shot)
plan->resume_stack = KosStackBlock_DeepCopy(to_use->resume_stack);
```

The plan gets its own DeepCopy. The continuation keeps its original copy for the next invoke.

**xfer_step_install (3545), xfer_step_push (3660):**

```c
// Before
new_stack = KosStackBlock_COW(plan->resume_stack);

// After
new_stack = plan->resume_stack;
```

The plan holds the DeepCopy; we assign it to the frame. Plan transfers ownership; the installed frame is the sole owner and may mutate.

### 7. KosStack_Copy (stack.c)

Currently: COW copy (bump block refcounts).

Proposed: `KosStackBlock_DeepCopy` of the block chain, then wrap in a new KosStack. This changes semantics: copy is now independent; modifications to the copy don't affect the original (same as user expectation, but implementation changes from COW to full copy).

---

## APIs to Drop or Simplify

| API | Action |
|-----|--------|
| `KosStackBlock_EnsureExclusive` | Remove (static, internal) |
| `KosStackBlock_CopyForWrite` | Remove (or make static and unused, then delete) |
| `KosStackBlock_COW` | Remove |
| `KosStackBlock_REFCOUNT` | Remove |

---

## APIs to Retain (possibly simplified)

| API | Change |
|-----|--------|
| `KosStackBlock_Release` | Simplify: always DECREF objs and free (or cache); no refcount check |
| `KosStackBlock_ReleaseExclude` | Keep; exclude logic still needed for re-entrancy |
| `KosStackBlock_ReleaseChainExclude` | Keep |
| `KosStackBlock_DeepCopy` | Promote to primary copy API |
| `KosStack_Copy` | Change implementation to use DeepCopy |

---

## Exclude Semantics (unchanged)

`ReleaseExclude(block, exclude)` skips DECREF on slots equal to `exclude`. Used when the continuation Python wrapper is deallocated and the stack may hold a reference to it—we avoid re-entering the wrapper's dealloc. This is independent of COW and remains necessary.

---

## Test Updates

### tests/native/test_stack.c

- Tests that assert "KosStack_Copy creates a stack that shares the same blocks" must change: copies will no longer share blocks. Update to assert deep-copy semantics (independent modification).
- Tests that rely on refcount behavior (e.g., COW copy then modify one copy) need new expectations.
- Any test that asserts `KosStackBlock_REFCOUNT` or COW behavior must be removed or rewritten.

### tests/native/test_dynchain.c

- "Test frame cloning with COW stack" (line 55): update for DeepCopy semantics. Cloned frame's stack is a full copy, not shared.

---

## File-by-File Change Summary

| File | Changes |
|------|---------|
| `koskari/lib/stack.h` | Remove `KosStackBlock_COW`, `KosStackBlock_CopyForWrite`, `refcount` / `KosStackBlock_REFCOUNT`; update `KosStack_Copy` docs |
| `koskari/lib/stack.c` | Remove EnsureExclusive, CopyForWrite, COW, refcount; simplify Release; remove refcount from New/NewSized; simplify mutating ops; KosStack_Copy uses DeepCopy |
| `koskari/lib/dynchain.c` | Replace all `KosStackBlock_COW` with DeepCopy or ownership transfer; update kos_dynframe_clone, capture, clone, plan build, xfer steps |
| `koskari/lib/dynchain.h` | Update doc comments (resume_stack no longer "COW reference") |
| `koskari/lib/eval.h` | Update "Clones the frame's stack using COW semantics" |
| `koskari/lib/cont.c` | No API changes; still uses `kos_continuation_free_exclude` |
| `tests/native/test_stack.c` | Update COW/share assertions for DeepCopy semantics |
| `tests/native/test_dynchain.c` | Update frame clone test |

---

## Migration Strategy

1. Implement `KosStackBlock_DeepCopy` and ensure it is robust (handles NULL, empty chain, multi-block chain).
2. Add a compile-time or runtime flag to toggle COW vs non-COW for gradual rollout if desired.
3. Update dynchain first: kos_dynframe_clone, capture paths, continuation clone, plan build, xfer steps.
4. Remove EnsureExclusive from all stack.c mutating ops.
5. Remove CopyForWrite, COW, and refcount from block structure.
6. Update KosStack_Copy.
7. Update tests.
8. Run full test suite.

---

## Cost Summary

- **Capture:** One DeepCopy of the chain (including frame stacks via `kos_dynframe_clone`); full PyObject INCREF pass. Continuation stores the cloned chain. Original stays frozen.
- **Invoke:** One DeepCopy of the chain (and thus stacks) for the installed state; full PyObject INCREF pass. Plan gets this copy; install transfers ownership to the frame. Continuation retains its frozen copy.

No refcount, no EnsureExclusive. Single-owner semantics throughout.
</think>
Continuing the design doc — the previous write was truncated.
<｜tool▁calls▁begin｜><｜tool▁call▁begin｜>
Read