# Stack Segment Simplification

**Status: Implemented (branch `simplify-stackblock`).  All gates green:
full suite, native, ASAN, Valgrind, plus a suite run with
`KOS_DEBUG_PATCH_DYN_CACHES=1` validating the back cache against the
walk (both flags now route through the setup.py passenv `-D`
mechanism).  Companion follow-up:
[dynnode-inline-segments.md](dynnode-inline-segments.md).**

## Purpose

Make `koskari/lib/stack.[ch]` tell the truth about what the operand
stack is today: a storage-only segment owned by a dynamic-chain node,
manipulated through the block-level API and the inline fast paths.
Everything else in the unit -- the `Stack` Python wrapper type, the
object-level `KosStack_*` API that serves it, and the intra-stack
block-chaining machinery -- is dead weight left over from the era when
the block itself carried control-flow metadata (BlockStyle, labels,
enabled flag) and the evaluator pushed and popped blocks to enter
regions.  That metadata moved to the dynamic chain (`KosDynRegion`
nodes) long ago; the code that expressed it never left.

This is a deletion-and-rename pass.  Growth, the freelist, the
fast-path inlines, and `KosStackSteal` keep their exact behavior.  The
single functional addition is a useful `back` link for unwinds.

## Current state

- `KosStackBlock` is a raw `PyMem_Malloc` struct: `back` pointer,
  `max`/`len`, flexible array of `PyObject *`.  16 slots at birth,
  realloc-doubling to a hard cap of 256.  Base-size blocks are cached
  on a freelist (reusing `back` as the link); grown blocks are freed
  outright.
- Each segment is owned by exactly one dynchain node (frame, with,
  try, finally, loop, foreach, collect, prompt).  The eval state holds
  a non-owning `KosStackBlock **` aimed at the innermost owner's stack
  field.  Continuation capture deep-copies; there is no sharing.
- The `back` chain is never built by the live evaluator.
  `KosStackBlock_PushBlock` / `PopBlock` / `DropBlock` have no C
  callers outside stack.c; `Pull` is reachable only through the Python
  wrapper.  Every `while (b) ... b = back` loop in Release / Traverse
  / DeepCopy iterates exactly once in practice.
- The `KosStack` Python type (`koskari.lib.Stack`) is test surface
  only: no C-internal user of `KosStack_New` / `KosStack_Check` exists
  outside stack.c, and the object-level `KosStack_*` functions exist
  to serve its methods.

## Changes

### 1. Drop the Python wrapper and object-level API

Remove `KosStackType`, `KosStack`, `stack_new` / `stack_dealloc` /
`stack_traverse` / `stack_clear`, every `stack_m_*` method, and the
registration in lib.c.  Remove the object-level `KosStack_*` functions
they front (`Push`, `PUSH`, `Extend`, `ExtendPtr`, `EXTENDPTR`,
`Peek`, `PeekAt`, `Pull`, `Pop`, `PopTail`, `PopTailDict`,
`PopTailDictNames`, `TailPtr`, `PopTailPtr`, `Drop`, `DropNoErr`,
`Duplicate`, `Replace`, `PushBlock`, `PopBlock`, `DropBlock`,
`Clear`, `Copy`, `New`, and the check macros).

Deliberate consequence: `tests/lib/test_stack.py` (the only Python
user of the wrapper) goes with it, and `tests/native/test_stack.c` is
rewritten as a slimmed segment-level suite covering push/pop/growth,
tail extraction, the steal API, deep copy, the freelist, and the
back-link reset invariants.  `tests/lib/opcode/test_stack.py` is
opcode-level and stays.

### 2. Drop the dead chain machinery

Remove `KosStackBlock_PushBlock`, `PopBlock`, `DropBlock`, and the
static `Pull` / `Replace` helpers.  Fix the stale doc-comment
reference in dynchain.h that still names `KosStackBlock_PushBlock`.

`DeepCopy` reduces to a single-segment copy: allocate, copy `len`
pointer slots, incref each.  This also removes the latent failure
where flattening a hypothetical multi-block chain of more than 256
total values would trip the grow cap.  `ReleaseChain` collapses into
`Release`; `Traverse` loses its outer loop.  (The freelist teardown in
`koskari_stack_clear` keeps its own walk over the freelist links.)

### 3. Rename

- `KosStackBlock` becomes **`KosSegment`**: the struct, and the
  accessor macros (`KosSegment_BACK`, `KosSegment_MAX`,
  `KosSegment_LEN`).
- Surviving API functions shorten from `KosStackBlock_X` to
  **`KosStack_X`**, uniformly `KosStack_X(KosSegment **, ...)`.  The
  stack is the abstraction; the segment is the storage.  The freed-up
  `KosStack_` prefix is unambiguous once the object-level API is gone.
- `KosStackSteal` and the `kos_stack_*_fast` inline names are already
  in the target convention and keep their names.
- File names stay `stack.c` / `stack.h`.

Survivors (to be confirmed by grep at implementation time; anything
that turns out callerless after the wrapper is gone gets deleted too):
`NewEmpty`, `Release`, `Traverse`, `DeepCopy`, `PushRef`, `PUSH`,
`PopRef`, `Peek`, `PeekAt`, `Drop`, `Drop1`, `DropNoErr`, `Duplicate`,
`PopTail`, `TailPtr`, `PopTailPtr`, `PopTailDict`, `PopTailDictNames`,
`PopTailDictFromNames`, `ClearBlock` (rename to `Clear`), the steal
trio, and the static `Grow` / `ExtendPtr` / `EXTENDPTR` helpers.

### 4. Repurpose `back` as an unwind cache

Today, finding the enclosing stack after popping an owning node -- and
pushing a delivery value to the parent extent
(`KosEvalState_ParentStackPushRef`) -- walks the dynamic chain for the
next node with a non-NULL stack (`kos_dyn_walk_live_from`,
`kos_evalstate_parent_stack_slot`).  The `back` field takes over that
job as a cache.  Its two consumers are distinct and must stay that
way:

1. **Uninstall restoration** (the primary role): when a node owning
   the top segment pops, `back` supplies the slot to re-aim the eval
   state's live block pointer at.  This use is read-only with respect
   to the parent segment.
2. **Parent delivery** (the exception, kept rare): pushing a value
   onto the enclosing segment while it is *not* the active one.  The
   preferred discipline is to avoid this entirely -- pop/retarget
   first, then push onto the now-active segment.  Pre-retarget parent
   pushes survive only where ordering forces them (the async
   `__exit__` paths in dyn_with, where the node must stay on the
   chain while delivery happens).  Every such site is a mutation of a
   non-active segment and should be individually justified; do not
   let new ones accumulate.

- **Type**: `KosSegment **back` -- the *owning slot* of the enclosing
  segment (the address of the parent owner's `stack` field), not a
  bare segment pointer.  The slot form is what consumer 1 needs
  anyway (the eval state holds a slot), and it is what keeps consumer
  2 correct when a delivery push grows the parent: Grow reallocs and
  must write the moved segment back through its owner's slot.  The
  freelist keeps using the field as its link (one cast).
- **Set on install only**: when a node owning a segment is pushed and
  the eval retargets its live block slot, the new segment's `back` is
  set to the previously live slot.  Cloned segments (`DeepCopy`) are
  born with `back = NULL`.
- **NULL means walk**: consumers fall back to the existing chain walk
  when `back` is NULL.  This keeps spliced and thawed chains
  (continuation resume, gen freeze/thaw, xfer INSTALL) correct without
  requiring eager link rebuilds across a spliced span: their segments
  simply start unlinked and the walk answers until re-install sets
  links again.
- **Validity rule**: a `back` link is meaningful only while its owner
  is on the live chain of the current eval state.  Any operation that
  detaches a span from the live chain (gen freeze, in-place delim
  displacement, transfer plans) must clear `back` on the segments it
  detaches, or rely on the clone-has-no-link property.

### 5. Invariant: a non-top segment never moves

The back-link scheme and the wider single-owner model rest on: **a
segment can only grow (and therefore move, via realloc) while it is
the live push target, and only the innermost segment receives
pushes.**  The one exception is a parent-slot delivery push (consumer
2 above), which writes through the owning slot and so tolerates the
move -- but each such site is a mutation of a non-active segment and
is tolerated, not endorsed; the set of them should shrink toward the
pop-then-push ordering, not grow.  State this in stack.h; assert it
cheaply where practical (e.g. debug-build check that Grow's argument
is the live slot or a delivery target).

## Non-goals

- No change to growth sizing, the 256 cap, freelist behavior, the
  fast-path inlines, or `KosStackSteal` semantics.
- No per-extent segment expansion and no inline mini-segments; that is
  the companion proposal
  ([dynnode-inline-segments.md](dynnode-inline-segments.md)), which
  builds on this pass.

## Validation

Mechanical rename plus deletions: full suite via `make test`, then the
native memory gates (`make test-asan`, `make test-valgrind`) since the
freelist and Release paths are touched.  The back-link cache must not
change observable behavior; the chain-walk fallback remains the
semantic definition.
