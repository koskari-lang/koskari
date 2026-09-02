# Copy-and-Patch Stencil Compilation

**Status:** Design proposal -- not yet implemented. Ultimate target for
native execution of `.ki` and `.kio` modules, superseding the
emit-C-and-invoke-a-compiler direction record archived at
[../archive/native-compiled-modules.md](../archive/native-compiled-modules.md).

**See also:** [../archive/threaded-dispatch.md](../archive/threaded-dispatch.md) (phase 2 defines
the musttail handler form this design compiles ahead of time),
[../archive/cps-consumer-api.md](../archive/cps-consumer-api.md) (the resume contract a
stencil chain's suspension points arm into), [bytecode-finalization.md](bytecode-finalization.md)
(the finalize pipeline this lowering extends), [../OPCODES.md](../OPCODES.md),
`tools/opimpl.py`.

## Summary

Compile every opcode handler, once, into a relocatable machine-code stencil:
the handler's body as real instructions, with a small set of recorded
patch sites (holes) where a constant, an operand, or a tail-call target
needs to be written in. At module finalize -- the same pass that already
builds the sealed `[code]` block and, under
[../archive/threaded-dispatch.md](../archive/threaded-dispatch.md) phase 1, the `KosSlot`
dispatch region -- walk the `(op, arg)` stream and copy each instruction's
stencil into a growing per-module buffer, patching in its real operand and
the address of the next instruction's stencil. The result is native code:
no interpreter loop, no indirect dispatch through a handler table, no
external C compiler. This is copy-and-patch compilation (Xu & Kjolstad;
the technique behind CPython 3.13's JIT), applied to a musttail-threaded
handler set that already satisfies its precondition -- every handler ends
in a tail call to its successor, so stitching stencils together by copying
bytes and patching the tail-call address *is* the compiled function.

## Why this replaces the AOT-to-`.so` design

The archived design ([../archive/native-compiled-modules.md](../archive/native-compiled-modules.md))
emitted each `Code` object as C source and handed it to the platform
toolchain, exporting the interpreter's handlers through a `PyCapsule`
table so generated `.so`s could call them without linking against a real
`libkoskari`. That bought portability (any C compiler) at the cost of a
build step per module, a versioned ABI capsule, and a wheel/toolchain
story for consumer projects.

Copy-and-patch removes the compiler from the loop entirely. Stencils are
generated once, at koskari's own build time, from the same
`opcodes.incl`-driven handler set the interpreter already uses. Loading a
`.ki` or `.kio` file performs the copy-and-patch pass as part of ordinary
finalize -- not an opt-in build hook, not a separate artifact with its own
load-order fallback chain. Every module gets a native form, unconditionally,
the same way every module gets a sealed bytecode block today.

What survives the transition, because it was never about the toolchain:
the native-frame ABI reuse, pc as the universal resume coordinate, the
segment-boundary invariant, and the loop-duties audit. Those sections of
the archived document remain the correct analysis; this document assumes
them and does not re-derive them.

## Stencil generation

A stencil is produced per opcode handler, at koskari's own build time, from
the musttail-threaded handler form ([../archive/threaded-dispatch.md](../archive/threaded-dispatch.md)
§2, §6): a function of `(KosEvalState *es, KosSlot *cur)` that ends by
tail-calling the successor slot's handler. Each handler is compiled as its
own translation unit; the compiled object's relocations against known
symbols -- the operand read (`cur->arg`), the cache slot, the tail-call
target -- are exactly the holes. This mirrors CPython's `Tools/jit`:
compile normally, then read the object file's relocation table instead of
hand-writing a patching format.

The output per opcode is:

- the instruction bytes (the stencil body),
- a list of `(offset, kind)` patch sites -- operand immediate, cache-slot
  pointer, tail-call displacement, constant-pool reference -- to fill at
  copy time,
- the stencil's length, so the copy pass can lay out consecutive stencils
  without re-deriving it.

This is generated data, versioned the same way `handlers.incl` is: `tools/opimpl.py`
becomes a second consumer of `opcodes.incl`, alongside the existing
handler-table generation, so the stencil set cannot drift semantically
from the interpreter or from phase 1's handler table. `KOS_OP_HANDLER_COLD`
handlers (large, rarely run) are excluded from stenciling and keep a
tail-call-out to the ordinary compiled handler function; the archived
document's cold-path exemption for large opcodes applies here unchanged.

## Finalize: copy-and-patch

The lowering runs in the same finalize window that populates `[cpool]`
and, under phase 1, the `KosSlot` region -- after emit and jump-target
validation, before the block's `mprotect` seal:

```c
for (uint32_t pc = 0; pc < code_len; pc++) {
  KosOp o = code[pc];
  const KosStencil *st = &kos_op_stencils[o.op];

  offsets[pc] = cursor;
  memcpy(native_buf + cursor, st->bytes, st->len);
  apply_patches(native_buf + cursor, st, o.arg, o.cache, /* successor addr: */
                native_buf + cursor + st->len);
  cursor += st->len;
}
```

Each instruction's stencil is patched with a direct address to the next
instruction's copy -- not an indirect load through a handler table.
Sequential opcodes chain via a baked-in tail jump; this is strictly
cheaper than phase 2's `musttail return next->fn(es, next)`, which still
computes `next` from the slot array at run time. Branch operands (`JUMP`,
`CHECK_NIL`'s not-nil arm) patch to `offsets[target]` once the whole
buffer's offsets are known, exactly as phase 1's `KosSlot.arg` is filled
after decode. A forward branch requires the two-pass shape the copy loop
already implies: lay out and copy first, resolve branch and call-out
patches second.

`THEEND`'s stencil is the fault handler unchanged; reaching it still faults,
preserving the barrier semantics phase 1 relies on.

## Storage: the executable arena segment

The unified bytecode block already carries `[code][positions][cpool][extra][extents][top]`
(and, under phase 1, the `KosSlot` dispatch region) as one arena
allocation, sealed read-only after finalize populates it. The native form
is a second, per-module arena segment with the same lifecycle -- populated
in a writable window, then sealed -- but `mprotect`'d executable
(`PROT_READ | PROT_EXEC`, not `PROT_WRITE`) instead of read-only.

This segment is not serialized. The `(op, arg)` stream in `[code]` remains
the sole source of truth that `.kio` persists and `soften` strips down to;
the native segment, like phase 1's `KosSlot.fn` and the per-site `cache`
index before it, is regenerated from that stream on every load. A koskari
upgrade or a stencil-set change never needs a migration story for stored
modules -- it just changes what finalize produces from bytecode that
hasn't moved. `pack_into`'s block relocation extends to this segment: a
copy-and-patch buffer built at one address and moved to another needs its
internal patches to be self-relative (displacements) rather than absolute
where the compiler allows it, so relocation is a `memcpy`, not a second
patch pass; where a patch kind cannot be made relative (an absolute
constant-pool pointer, which is process-local like `[cpool]` already is),
`pack_into` re-patches that site the same way it already fixes up `[cpool]`.

## Externally-enterable pcs

Exactly as in the archived design, resumption addresses a pc, not an
instruction offset the copy pass happens to produce: `handler_pc` on TRY
and FINALLY, `pop_pc` on prompts, `resume_pc` on cc nodes, the pc after
every call/await/yield site, `saved_pc`/`saved_acting_pc` in the delimited-continuation
INPLACE surgery. The copy-and-patch pass records `offsets[pc]` for every
pc regardless of whether it is externally enterable (the array falls out
of the layout loop for free), so continuation resume, the raise walk, and
frame revival jump directly into `native_buf + offsets[pc]` the same way
today's dispatch loop sets `es->pc` and re-enters. A native frame handed a
pc outside the legal resume set is a chain-surgery bug either way; nothing
about stenciling changes that failure mode or its severity.

## Interaction with the CPS consumer API

[../archive/cps-consumer-api.md](../archive/cps-consumer-api.md) already states that a
musttail-threaded opcode handler and a `KosResumeFn` share a calling
convention, and names the boundary explicitly: bytecode's own resume
becomes one builtin -- push the delivered value, re-enter dispatch at the
pc -- at which point CALL and the await opcodes go through the same CPS
entries native code uses.

Stenciling does not add a second resume mechanism. A stencil chain's call
and await sites are ordinary `KosEvalState_CallResult` / `KosEvalState_NextResume`
invocations, arming the resume on the frame exactly as an interpreted
handler would; the trampoline never invokes a resume on the native code's
own C stack any more than it invokes one on the interpreter's, for the
same reason (a synchronous CPS loop must cost constant stack, not
constant-plus-one-native-frame). When the delivered value arrives, the
frame's armed resume re-enters at `native_buf + offsets[pc]` the same way
any other resumption does. The consequence: once CPS consumer API adoption
reaches its "bytecode's builtin resume" phase, that phase and this
document's finalize lowering are the same piece of work, entered from
either direction -- the CPS migration's last phase, or this design's first
real integration point.

This is also why the segment-boundary invariant is not a new obligation
here. CPS consumer API's "state lifetime" rules (borrowed vs. owned `void *`,
never a bare handler-freed allocation) and threaded-dispatch phase 2's "no
continuation-relevant state lives only in argument registers across a
suspension point" are the same rule stated for two different mechanisms.
A stencil is a compiled phase-2 handler; anything the phase-2 audit proves
about a handler's register discipline is proven about its stencil by
construction, since the stencil's instruction bytes come from compiling
that same handler body.

## Loop duties

Between the interpreter's opcode dispatches, the loop does more than
fetch: transfer-plan drain, pending-exception poll, GIL poll at back-edges,
`acting_pc` maintenance, trace/safetynet wraps. A stencil chain runs
without returning to that loop between instructions, so each duty is
either:

- folded into specific stencils (a GIL poll stencil spliced in at back-edges
  -- loop-closing branch targets, identifiable at copy time from the same
  jump-target information finalize already validates), or
- deliberately skipped, matching the archived design's stance: native
  execution bypasses `KOS_EVAL_SAFETYNET` and verbose-debug tracing;
  debug builds fall back to interpreting the `[code]` stream the stencil
  segment was derived from.

`acting_pc` must stay honest regardless -- the raise walk and extent
lookups key off it, so every patched stencil that can raise or suspend
writes it before doing so, matching the interpreter prologue's contract.
This is the same audit item the archived design flagged as its one real
unknown; it is not resolved by switching from emitted C to stencils and
must still be done here.

## MVP scope

1. **Stencil extraction tool.** Compile the musttail handler set (requires
   [../archive/threaded-dispatch.md](../archive/threaded-dispatch.md) phase 2 to exist first --
   this design has no meaning against phase-1 handlers, which return to a
   loop rather than tail-calling a successor) as individual translation
   units; parse relocations into `(offset, kind)` patch records; emit
   `kos_op_stencils[]` generated from `opcodes.incl`, alongside
   `handlers.incl`.
2. **Copy-and-patch finalize step.** The layout loop above; branch and
   call-out patch resolution in a second pass once all offsets are known;
   the executable arena segment added to `code_block_layout` with its own
   protection lifecycle.
3. **`pack_into` extension.** Relative patches relocate for free; absolute
   patches (constant-pool pointers) get a second pass mirroring the
   existing `[cpool]` fixup.
4. **Differential testing.** A slice of the corpus and phases 6/8 agree
   between interpreted and stencil-compiled execution; a coroutine awaits
   inside a stencil-compiled frame; a call/cc captured inside one resumes
   twice through clone (the delim INPLACE worked example from the archived
   design is the right first hand-verified scenario here too).

This is sequenced strictly after threaded-dispatch phase 2 lands and is
measured against real handlers, not projected -- phase 2's own performance
section already warns that a naive tail-call loop is not a foregone win
over the threaded-dispatch baseline; stenciling is the step that is expected to
recover it; that expectation is worth measuring, not assuming.

## Open questions

- Stencil granularity for opcodes with a hot inlined fast path and a cold
  fallback (the pattern `always_inline` handlers use today): one stencil
  with an internal branch, or two stencils selected at copy time based on
  a finalize-time hint. Affects code size and copy-pass complexity.
- Whether the back-edge GIL-poll splice is a distinct stencil variant per
  opcode (doubling the stencil set) or a small fixed poll stencil inserted
  as its own chain link at identified back-edges.
- Constant-pool patch representation: a `[cpool]` index baked into the
  stencil at generation time (fixed slot count assumption) versus a
  runtime lookup, given `[cpool]` is process-local and populated at
  finalize like the rest of the block.
- Every opcode stencilable is the goal, not a subset. `KOS_OP_HANDLER_COLD`
  handlers are excluded here because their current bulk makes stenciling
  them a poor first step, not because some opcodes are permanently
  unstencilable. YIELD_FROM is the flagship case: it carries `_COLD` for
  size, and [gen-yieldfrom-passthru.md](gen-yieldfrom-passthru.md) §10
  argues that CPS-based delegate-stepping slims it to normalize/install
  plus a reconstruction target -- small enough to stencil normally. The
  question this leaves open is whether any handler stays legitimately
  too large to stencil after that kind of slimming, or whether `_COLD`
  turns out to name a to-do list rather than a permanent category.
- Code-size and icache behavior of a per-module executable segment at
  scale (many small modules vs. few large ones); no measurement exists
  yet.
