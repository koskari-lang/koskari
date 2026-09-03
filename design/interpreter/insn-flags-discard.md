# Instruction-record flags: a finalize-time "result discarded" bit

## Status

Proposal -- not implemented.

## Summary

`KosOp` is 16 bytes on LP64 with two of them padding after `cache`.
Fill them with a `flags` word assigned at finalize, and use the first
bit to record that the instruction's pushed value is discarded by the
`POP,1` that follows it. The call core then tests a bit on the record
it already holds instead of fetching the next record and comparing its
opcode and argument at run time.

## Today

`OP_TRY_FUSE_POP1_AFTER_PUSH` in `koskari/lib/eval/eval_utils.c` runs
on every settled call result: load `code[pc]`, compare `op` against
`KOS_OP_POP`, compare `arg` against 1, and on a match release the value
and set the PC past the POP. The lookahead is a dependent load off the
code block on the hottest result path, and the elision is invisible to
anyone reading the instruction stream: the POP is still there, and only
the handler's run-time compare knows it will be skipped.

## Proposal

- Add `uint16_t flags` to `KosOp`, occupying the padding. Sixteen
  bytes stay sixteen; `KosOp_FN`, `KosOp_OP`, `KosOp_ARG`, and
  `KosOp_CACHE` are untouched.
- Define `KOS_OPF_DISCARD` (bit 0): the value this instruction pushes
  is consumed by the immediately following `POP,1`.
- Set it in `code_fill_handlers` (`koskari/lib/bytecode.c`), the pass
  that already walks the block once at finalize to assign `fn`. The
  rule is the same one the macro applies at run time, moved to finalize
  time. Like `fn` and `cache`, `flags` is derived on every load and
  never serialized; soften still emits only `(op, arg)`.
- Replace the macro body with a test of `KosOp_FLAGS(insn) &
  KOS_OPF_DISCARD` on the handler's own instruction, followed by the
  same release and PC skip. The skip stays: the POP record is still in
  the stream, and the thread fetch after the call reads `code[pc]`.

Setting the bit in C keeps the Python compiler's rule set separate from
finalize's: the compiler emits the POP as it does now, and the C side
decides what the record means at run time.

## Costs and open questions

- Which producers get the bit. Only the call family fuses today, so
  only its handlers would test it; setting the bit on every instruction
  followed by `POP,1` is harmless for handlers that ignore it, and
  costs nothing beyond the finalize walk.
- Whether a jump target can sit between a producer and its POP. It
  cannot today (the peephole would already be wrong), but the finalize
  pass should assert it against the jump-target set it validates.
- The remaining fifteen bits are room for other finalize-derived facts
  (a back-edge marker for the stencil work, a "next is a tail-position
  RETURN" bit for promotion). Each is its own proposal; the field is
  the enabling change.

## Related

- [bytecode-finalization.md](bytecode-finalization.md) -- the finalize
  pass this extends.
- [stencil-compilation.md](stencil-compilation.md) -- a consumer of
  per-record facts once they exist.
- [../EVALUATOR.md](../EVALUATOR.md) §2 -- the call result path.
