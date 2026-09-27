# Result destinations: an operation's result stored where it is going

**Status:** open. The user ruled (2026-09-27) that this is a
destination-encoding question, not one class of fused opcodes, and wants
the encoding discussed before any fusion is built.

## The cost

An operation whose result is immediately stored allocates the result,
pushes it, and a separate store row pops it into its home while the
home's old value dies:

```
OBJECT_OPERAND +      ; allocates the sum
FAST_STORE j          ; slot j's old int dies here
```

`define j (+ i 1)` is two allocations and two deallocations per
execution when one would do. The opgrams over the churn kernels show
`OBJECT_OPERAND FAST_STORE` at 42 sites, and pidigits runs six
`FAST FAST_STORE` slot moves per iteration; fannkuch has the same shape.
Estimated 3-6% on loop, pidigits, nbody, fannkuch.

Commit 4014c52c (3.1 in the perf brainstorm) already reuses a dying
operand as the result's home: `kos_long_fast_result(res, reuse, reuse2,
out)` takes up to two candidates with refcount 1 and matching type.
The destination's old value is the third candidate, and the row does
not know its destination.

## The destination space

Discarded (`POP 1`, handled by the join-constant elision and fused
`_POP` twins), a FAST slot, a VAR name, an attribute, an item, and the
permutations of these with the source encodings already in use. The
source side is spelled as an opcode prefix: stack unnamed, `VAR_`,
`FAST_`, `CONST_`. A destination spelled as a suffix would mirror it
(`OBJECT_OPERAND_TO_FAST`), and the count multiplies: every operation
row times every destination.

## Three encodings to choose between

1. **A destination operand on the row.** One extra immediate on the
   operation rows: destination kind plus index. The handler stores
   through a small switch at the end. One row family, no new opcodes;
   a decode and branch per execution, and every operation handler
   grows by the store arms unless the store is a tail call into a
   per-destination finisher.
2. **Fused twins per destination.** `OBJECT_OPERAND_FAST_STORE` and
   friends, produced by `fuse_feeders` rules like the `_CONST` twins
   from 2.2. No per-execution decode; the opcode table grows by the
   product, and each twin needs its own elector family or shares one
   through a body macro.
3. **Elector body variants.** The elector already chooses a body by the
   site's operand types (`eval/eval_electors.c`). If the destination is
   a site fact recorded at finalize (the next row is a store into slot
   k), the elector can choose a storing body, and the store row becomes
   a no-op the body skips over. No new opcodes, no decode on the hot
   path; the instruction stream must stay sealable
   ([electors.md](electors.md) Summary), so the skip has to be a pc
   fact the body computes, not a rewrite.

The user's stated preference is an encoding for "stored in this
manner", which is 1 or 3, not 2.

## What the reuse needs at the destination

- The destination's current value with refcount 1 and the result's
  type: an int result into a slot holding a two-digit int reuses the
  object in place; a float likewise.
- FAST slots hold raw values or promoted cells (a closure promoted the
  slot); a store into a cell goes through the ref and cannot reuse.
  `kos_ns_is_raw` on the slot decides.
- VAR and ATTR destinations resolve through namespaces or descriptors;
  reuse there is a dict or instance-dict value with refcount 1, which
  is rarer. FAST slots are where the win is.

## Measurement plan

Baseline before the first edit, on a build proven current (touch
setup.py, rebuild with a `uv run` probe, confirm with `nm` or `size`):
instruction and cycle counts for loop, pidigits, nbody, fannkuch, and
`tox -qe opgrams -- --group churn --top 30` for the pair counts. The
3.1 tests in `tests/lib/opcode/test_operand_reuse.py` show the shape a
reuse test takes: a shared left operand must never be rewritten.

## Constraints from the compiler

A fused or annotated row must keep the label census and jump rules
that `elide_join_constants` and `fuse_feeders` obey: nothing crosses a
label, and a row that is a jump target is never fused into its
predecessor. The compiler keeps every transformation mapping the input
form cleanly to the final form.
