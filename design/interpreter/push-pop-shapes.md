# Node push and pop by shape

**Status:** investigative. Written 2026-09-27 after closing the loop-entry
item (commit 96e1cf42), which removed the per-execution extent lookup
from the region pushes. What remains of a loop entry's cost is the push
and pop machinery itself, and this note records the one idea for it
that survived the discussion, with its exact blast radius. Nothing here
is approved.

## The cost that remains

A probe on the current build (`with_loop` against `with_when`, one
`(while True ... (break))` per outer iteration against one `(when True
...)`): 22.5 ns against 12.9 ns per iteration, so 9.6 ns for a LOOP
push, a BREAK, and the pop it causes. Before 96e1cf42 the same figure
was 14.6 ns.

What `PUSH_LOOP` runs today, in order:

1. `kos_dynloop_new`: a node from the freelist, `kos_dynnode_init`
   (parent, kind, flags, caps, id, stack NULL, ns NULL), the two
   targets, `enabled`, `result`, `kos_dynnode_use_inline_segment`.
2. `kos_dyn_push`, which is `kos_dyn_prepare_push` then
   `kos_dyn_resync_after_push`:
   - `kos_dyn_prepare_push`: `kos_dyn_ensure_region_segment`, a switch
     on the kind that attaches the inline segment for PARAM, NS, WIND,
     and COLLECT and does nothing for the kinds whose `_new` already
     attached one; then the namespace fill, an incref of the live
     namespace into the node when it carries none.
   - `kos_dyn_resync_after_push`: the sync trace bracket; the frame
     branch (a frame takes its own registers, a region takes the live
     frame); the namespace fallback branch; the segment branch (own
     segment, or inherit the live block); the segment stamp
     (`kos_dyn_stamp_installed_segment`, recording the displaced block
     slot); then `kos_dyn_resync_apply`, which compares the frame
     against the live one, finds it equal for every region, and stores
     block and namespace.
3. In the handler, an incref of None into the result.

What `POP_LOOP` runs: the top check, an incref of the result,
`kos_dyn_pop_loop`, which is `kos_dyn_commit_dyn_top` then the free.
`kos_dyn_commit_dyn_top` calls `kos_dyn_resync_after_pop`, which
derives `need_frame` (never true for a region), `need_block` (true for
a region that owned the live block), and the namespace reconcile
(always "unchanged" for a region, whose ns is a fill of the live one),
before reaching the block-only restore branch. The free decrefs the
result, releases the segment, decrefs the namespace fill, and returns
the node to the freelist.

Every branch in the push's resync and in the pop's derivation is
decided by facts fixed at construction: whether the node is a frame,
whether it owns a segment, whether its namespace is its own.

## The idea

Not a push per kind. `kos_dyn_push` and `kos_dyn_resync_after_pop`
keep their names, callers, and signatures, and each gains two arms
chosen by a fact the node already carries:

- **frame** (`KosDynNode_IS_FRAME`): today's path, unchanged.
- **region** (everything else): the namespace fill; if the node owns a
  segment (`KosDynNode_STACK` non-NULL), the segment stamp and the block
  store; the dyn top store. Frame, namespace register, code, and pcs are
  not touched. That is the region path today with the frame comparison,
  the namespace fallback, and the `kos_dyn_resync_apply` call removed.

For the pop: when the removed node is not a frame and the survivor is
its parent, restore the block from the removed segment's back link when
that segment was the live block, take the namespace from the survivor's
fill, and return; when the back link is missing (a cloned node grafted
by a continuation carries none), fall through to the walk that exists
today. The frame arm is today's code.

The shape is read from the node (`kind`, `stack`), not stored again. No
new caps bit, no new field.

**The stop rule.** No kind gets a push or pop function of its own. A
handler that knows its node's kind calls the same `kos_dyn_push`; the
gain is that the compiler folds the shape test when the constructor is
inlined beside it. A proposal for `kos_dyn_push_<kind>` is answered
with "which arm does it not fit, and why".

## What this does and does not touch

Does: the bodies of `kos_dyn_push`, `kos_dyn_resync_after_push`, and
`kos_dyn_resync_after_pop` in `dynchain.c`.

Does not: any constructor, any `_clone` constructor,
`kos_dyn_ensure_region_segment`, `kos_dyn_clone_copy_segment`, any
handler, any node field, any opcode.

## The constructor and clone question, spelled out

During the discussion a further step was proposed and then withdrawn:
making PARAM, NS, WIND, and COLLECT attach their inline segment in
`_new`, as LOOP, FOREACH, TRY, FINALLY, PROMPT, and WITH do, so that
`kos_dyn_ensure_region_segment` and its kind switch could be deleted
from the push. It is recorded here so it is not re-proposed without
these facts:

- The switch costs one kind compare and a branch per push. Deleting it
  is tidiness, not a measured saving.
- `kos_dyn_clone_copy_segment(cloned, src)` copies the source node's
  operand values into a continuation clone, and it returns without
  copying when the clone already has a segment
  (`if (KosDynNode_STACK(cloned) || ! seg) return 0;`). Every `_clone`
  constructor therefore leaves `stack` NULL. This is how continuation
  capture works today and is not affected by changing `_new`, which the
  clone dispatch (`kos_dyn_clone_dispatch`) never calls.
- The comment on `kos_dyncollect_new` says the deferred attach is so "a
  clone built through this constructor" arrives stackless. The dispatch
  uses `kos_dyncollect_clone`, so the stated reason no longer holds;
  whether an older path still reaches `_new` for a clone has not been
  checked.

If that step is ever taken, it consists of exactly: adding
`kos_dynnode_use_inline_segment` to `kos_dynparam_new`, `kos_dynns_new`,
`kos_dynwind_new`, and `kos_dyncollect_new`; deleting
`kos_dyn_ensure_region_segment` and its call in `kos_dyn_prepare_push`;
rewriting the `kos_dyncollect_new` comment; and confirming by test that
a continuation captured inside each of the four regions still carries
the region's operand values (the copy contract above is untouched, so
this is a check, not a change). It is not part of the idea in this
note and should not ride along with it.

## Measurement

Baseline on a build proven current (touch `setup.py`, rebuild with a
`uv run` probe, confirm with `size` or `nm`): the probe above (9.6 ns
per entry), then fannkuch, nbody, binary_trees, list_each, and tree,
instruction and cycle counts, against `tmp/perfstat-3.8-after.log` or
its successor. The change touches no opcode form, so no new tests are
needed beyond `tests/lib/opcode/test_region_targets.py` and the
continuation suites, which exercise cloned regions.

## Related

- [electors.md](electors.md): the sealed instruction stream the region
  targets now point into.
- [call-path-followups.md](call-path-followups.md): the rule from the
  inlining item, which applies here too: keep loop bodies out of the
  RETURN and CALL handlers and the preserve-none trampolines.
