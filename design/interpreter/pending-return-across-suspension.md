# Pending return across a suspended leave thunk

Status: work in progress on branch `transfer-plan-rework`. The first cut is
committed with known defects listed below; it must not merge as it stands.

## The defect

A `return` unwinds the dynamic chain from the returning frame toward its
parent. When a node's leave thunk suspends on a Python awaitable (a
`dyn-wind` after thunk, a `with` block's `__aexit__`), the walk stops and the
return value has nowhere to sit: the returning frame's operand stack is behind
the thunk, and the parent has not been reached.

Before this work `kos_dyn_unwind_return` released the value on that branch and
`eval_event_return` then stored the same pointer, unowned, in
`KosContext.pending_return_value`. Nothing read the field. On revive the frame's
bytecode continued past its `RETURN`, ran `POP_WIND` and the function's final
`RETURN` against an empty stack, and raised `RETURN: stack underrun`.
`KosContext_Free` then released a reference the context never held.

Reproducer, driven by hand:

```koskari
(lambda [v] (dyn-wind [] (return v) (call park)))
```

where `park` is an awaitable that yields once. The first `send` parks; the
second must finish with `v`.

A parked exception on the same path has long been handled through the
per-node exception stash (`kos_ctx_exc_stash_*`). The return had no
equivalent.

## What the first cut does

- The unwind walks (`kos_dyn_unwind_return`, `kos_dyn_unwind_to_node`,
  `kos_dyn_unwind_to_frame` in `dynchain/dyn_walk.c`) leave the value owned by
  the caller on the async branch and record the id of the node whose pop
  suspended in `KosContext.pending_return_node_id`.
- `KosEvalState_SetPendingReturn` consumes the value into the context together
  with a `KosPendingReturnKind` naming the return path to re-enter (bytecode
  frame, native frame, delimited route) and, for the delimited route, the
  bookmark node.
- `eval_resume_pending_return` re-enters that path with the parked value. It
  fires from the control-returned hook and from the entry phase after an
  await revive, once `kos_dyn_chain_has_node_id` reports the recorded node
  gone. Ids are compared, never the freed pointer.
- `kos_dyn_complete_pending` also fires a wind's control-returned hook when the
  wind is at the top of the chain in a pre phase at revive: a thunk that
  tail-called the awaitable has no frame left to return through.
- GC: `KosContext_Traverse` visits the pending value and walks the suspended
  plan stack (review item H6). `KosNamespace_TraverseUntracked` reports an
  untracked namespace chain through its holder, called from
  `kos_dynnode_traverse`.

Tests: `tests/compile/specials/test_context_gc.py`.

## Known defects in the first cut

From the read-only review of the uncommitted diff, ranked.

1. **Over-attribution in GC.** `kos_dyn_prepare_push` gives every non-frame
   node an owned reference to the live namespace, so the untracked-namespace
   report now runs once per node above the frame. Frees a live object on
   3.9-3.11, corrupts GC header bits on 3.12+. Report from `FRAME` nodes only,
   or once per distinct namespace. Blocker.
2. **Return from a called frame fails on revive.** The re-entered return path
   ends in `EVAL_DISPATCH` for a non-root frame; the entry phase does not map
   that kind and `koskari_eval` treats it as terminal. Map `DISPATCH` with
   `NORMAL`. Blocker.
3. **A second park overwrites the first.** A return inside the after thunk
   that crosses another suspending node parks while the outer return is
   pending; `Py_XSETREF` drops the outer value. The pending return must be a
   LIFO stack, as the plan stack is.
4. **A raising after thunk leaves a stale return.** The raise walk pops the
   node without clearing the parked return; the next hook or revive resumes a
   return the exception should have replaced. Discard the pending return when
   the raise walk pops its node.
5. **Double release on walk errors** (pre-existing). The walk releases the
   value on error exits and both callers release again. Contract to adopt: the
   walk never releases the value except into a finally block.
6. **A `RAISE` from the resume on the hook path escapes without
   `process_raise`**; the entry-phase path routes it correctly.
7. **The walkers write the node id whether or not anyone parks.** Harmless for
   today's callers; move the scratch id onto the eval state and copy it into
   the parked entry.
8. **Resume under a live transfer plan.** A wind detached by a nested
   continuation transfer reads as gone and resumes early; a continuation clone
   carrying the same node id keeps the return parked forever. Cheap guard: no
   resume while a plan is active. The clone case needs the transfer-plan
   rework this branch is for.

Untested shapes: return from a called function, return inside the after
thunk, raising after thunk, body-value `POP_WIND` with a tail-calling async
after thunk, `with` whose `__aexit__` awaits, delimited and native return
kinds, collection where a caller frame's namespace holds the coroutine.

## Direction

A pending return is one more kind of suspended obligation on a context, next
to the parked exception and the suspended plans. The rework should give the
three one shape: a per-context stack of obligations, each tagged with the node
whose completion releases it, pushed by the walk that suspends and popped by
the hook or revive that sees the node gone, with the raise walk discarding
obligations owned by nodes it pops. Continuation capture then has one place
to clone or forbid.

Related: [callcc.md](callcc.md),
[extent-wind-unwind.md](extent-wind-unwind.md),
[archive/interrupted-transfer-plans.md](../archive/interrupted-transfer-plans.md).
