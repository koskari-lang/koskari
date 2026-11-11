# Delimited handler frames: ownership and SHIFT-pop provenance

## Status

Implemented, in revised form: the handler frame is snapshotted at
invoke rather than pinned, and the clone/provenance questions are
subsumed by instance identity
([prompt-instance-identity.md](prompt-instance-identity.md)). The
case analysis and diagnosis below remain the record of the underlying
asymmetry. Companion to [callcc.md](callcc.md) and
[../archive/interrupted-transfer-plans.md](../archive/interrupted-transfer-plans.md).

Reproducers live in `tests/reproducers/test_delimited_cross_frame.py`;
the shape matrix is `tests/sweep/delimited_sweep.py`.

## The three cases a rule has to cover

A `shift` pushes a SHIFT node delimiting the **handler body**, and `POP_SHIFT`
at the end of that body pops it. Who pops it, and what must stay alive for
them to do so, differs by shape.

| # | Shape | Handler runs in | Continuation exits the handler's frame? |
| - | ----- | --------------- | -------------------------------------- |
| 1 | prompt and shift in the same frame | the prompt's frame | **No** -- it ends at POP_PROMPT inside that frame |
| 2 | frames interleaved between prompt and shift | a callee frame | **Yes** -- the callee's return is part of the continuation |
| 3 | k stored, invoked after the prompt unwound naturally | nothing; no live handler | N/A -- the SHIFT on the chain is a clone from the graft |

Case 1 needs nothing extra. Case 2 is the frame-lifetime problem this
note diagnoses. Case 3 runs entirely on the graft, where instance
identity distinguishes its SHIFT clone from a live handler's.

### Case 3 has an eval-loop boundary limit

Invoking an escaped delimited continuation works when the invoke happens in
the **same top-level form** as the capture, and raises
`KoskariContinuationApply` otherwise:

```koskari
;; works -- capture and invoke inside one form
(define r
  (let [[captured None]]
    (begin
      (set! captured (reset (+ 1 (shift k k))))
      (captured 5))))          ;; => 6

;; raises KoskariContinuationApply -- invoke in a later top-level form
(define captured (reset (+ 1 (shift k k))))
(define r (captured 5))
```

**`KoskariContinuationApply` is the undelimited mechanism.** A delimited
continuation reaching it is a misroute, not a designed boundary: the
delimited invoke falls through to the undelimited transport once its
own path is unavailable. The failing shape above is a defect, just not
one this note addresses.

What it exposes: post-unwind invocation stitches the captured segment
onto the current chain so resumption arrives where it belongs. That stitch
is reachable while the capturing top-level form is still on the driver.
Once that form completes, the driver has returned and re-entered, the
stitch path is no longer available, and the invoke degrades into the
upwards-only escape -- which is undelimited machinery and cannot arrive
back at the invoke site. Frame depth is irrelevant: invoking from inside
a function, or inside a fresh `reset`, fails identically. The boundary
is the eval-loop entry, not the frame.

The general delimited case needs to **yoyo** between the invoke site and the
captured point, which is not a purely upward path, so it cannot be served by
the same mechanism. Any future work here should treat the upwards-only escape
and the in-place yoyo as distinct transports rather than trying to widen one
into the other.

The asymmetry is the whole problem. In case 2 the callee frame does two jobs
at once: it hosts the handler body, and it is part of the captured
continuation. Resuming runs the callee's remainder and then **returns out of
the very frame the handler still has to come back to**. Case 1 never notices
because the handler's frame is the prompt's frame, which the continuation
terminates inside rather than exiting.

## The case-2 failure mode

Timeline, with F the callee frame, absent the snapshot:

1. handler body runs in F
2. `(k v)` invoked -- `kos_delimited_invoke_inplace` displaces F's stack/pc
   with the captured resume state and saves the handler's stack/pc on the
   delim (`saved_stack`, `saved_pc`, `saved_acting_pc`)
3. the slice runs **in F** (the resumed callee remainder), so F must be on
   the chain and current
4. the slice's callee-return exits F. `kos_dyn_walk_return` loops
   `while (dyn_top != frame)`, so it stops at F itself and never reaches the
   DELIM below it -- the continuation is **not** truncated, a natural
   suspicion that the trace disproves. F is popped **and freed** to the
   node freelist.
5. the caller's remainder runs, reaches POP_PROMPT, routes to the delim,
   and calls `kos_dyn_inplace_restitch`
6. the handler was supposed to continue at the invoke site, then POP_SHIFT

Step 4 invalidates everything step 5 depends on. A restitch that borrows
F does three wrong things:

* `ret_frame = KosDynDelim_RETURN_FRAME(delim)` points into freed
  memory, and `KosDynFrame_PC(ret_frame)` / `ACTING_PC` are written
  through it
* resolving `resume_frame` by walking **at-or-below the prompt** finds
  the *caller's* frame, so the handler's `saved_stack` is restored over
  the caller's stack (the same wrong walk the invoke side has to avoid
  by resolving the frame owning the SHIFT)
* the stashed span, SHIFT included, is re-linked above the prompt with no
  frame for the handler to run in

The observable end state is a SHIFT node at `dyn_top` when the enclosing
scope's POP_NS runs -- which is why every `kos_dyn_pop_*` verifies the
kind of the node it is about to tear down and names a mismatch instead
of dereferencing it.

## What the delim has, and what it lacks

The evaluator needs four things to resume the handler: **code**, **ns**,
**block**, **pc**.

| item | where it lives |
| ---- | -------------- |
| block | `delim.saved_stack` -- on the delim, **owned** |
| pc | `delim.saved_pc` / `delim.saved_acting_pc` -- on the delim |
| code | only on the frame node (`KosEvalState_CODE(es) = KosDynFrame_CODE(ft)`) |
| ns | innermost NS provider on the chain |

So half the state is saved on the delim and the other half depends on a
frame node.

**Rejected: add `code` and `ns` fields to the delim.** Consumers resolve the
current frame by walking the chain for a FRAME -- `kos_chain_first_frame`,
`KosEvalState_FRAME`, exception dispatch. A handler running with code and ns
reconstituted from the delim but no FRAME on the chain breaks those, and
synthesising one to satisfy them re-invents the node. The delim arm is also
already the largest in the `KosDynNode` union, so two more pointers grow
*every* node in a freelist-hot allocator, to duplicate what a node already
carries.

**Rejected: pin the live frame (unlink-not-free).** Pinning threads a
special lifetime state through the frame pop path and the freelist, and
holds a frame un-freed across the slice for the benefit of one resume.

**Resolution: snapshot the handler frame at invoke.** When a FRAME sits
between the SHIFT and its prompt, `kos_delimited_invoke_inplace` clones
it (`kos_dynframe_clone` is exactly the compacted baseline: code,
revealed ns, deep-copied operand block, pc) and records the copy as the
delim's return frame. Restitch pushes the copy above the prompt and
hangs the stashed span off it, so the handler has a frame, the
frame-walkers resolve, and the final pc load fires. Nothing is pinned
and no frame outlives its natural pop -- the original dies with the
slice, which is what copying at invoke is for. A handler sharing its
frame with the prompt is untouched: the continuation ends inside that
frame at POP_PROMPT rather than exiting it.

## The pop rule

> **A SHIFT node is popped by whoever pushed it. A live handler resumes
> in a frame guaranteed to exist: its snapshot.**

| case | who pops SHIFT | what makes it safe |
| ---- | -------------- | ------------------ |
| 1 | POP_SHIFT in the prompt's frame | nothing extra |
| 2 | POP_SHIFT in the handler's snapshot frame | the snapshot, pushed by restitch |
| 3 | delim routing on the graft | instance identity: the cloned SHIFT resolves its falsified prompt and delivers through the paired delim |

Case 2 with multi-shot is case 2 plus case 3: only the first invoke runs
in place, and re-invokes take the PUSH graft, where the handler frame is
part of the instantiated template rather than live at all.

## Open questions

* GC traversal of the snapshot while it is held on the delim and not yet
  on the chain: it carries a `KosCode` strong reference, so the delim's
  traversal must cover it or the collector can miss the cycle.
* [OPCODES.md](../OPCODES.md) §20's account of the prompt's stack-block
  ownership and of the continuation's extent needs revising to match
  this design.

# The end.
