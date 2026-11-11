# Resolved operands: pointers in the instruction record and the extras row

**Status:** Implemented (2026-09-04, branch
`claude/opcode-electors-caching-c8142d`): 8fa4b90b the record and the
row, 3311feb6 the fetch and the jump, 0faf04f4 a row's LABEL fields as
target records. Two decisions taken during the work and not in the
text below: the reserved `flags` word became the record's own `pc`, so
every pointer to an instruction names its index and a code object is
bounded at 65535 instructions; and the `.kio` ABI was not bumped, since
opcode values and the softened stream are unchanged. Measured on the
tree kernel against the branch base, 2.1% fewer instructions and 1.3%
fewer cycles; on the call-free loop kernel, 2.0% fewer instructions
and 5.0% fewer cycles. The permanent description is
[../interpreter/bytecode.md](../interpreter/bytecode.md) and
[../EVALUATOR.md](../EVALUATOR.md) §2.4 and §2.5.

**See also:** [../interpreter/bytecode-finalization.md](../interpreter/bytecode-finalization.md)
(the finalize pass that derives the pointers),
[insn-flags-discard.md](insn-flags-discard.md) (superseded: the padding
it wanted is the pc), [../interpreter/stencil-compilation.md](../interpreter/stencil-compilation.md)
(the same values as patched immediates), [threaded-dispatch.md](threaded-dispatch.md)
(the `KosOp` record and the fetch), [../interpreter/electors.md](../interpreter/electors.md).

## Summary

Every operand a handler reaches today is an index resolved through the
eval state and then through the code object: `es->code`, a table pointer
on the code, the entry. The fetch itself walks `es->code->code[pc]` on
every dispatch. The handlers are separate functions handed three
registers of context, so none of those loads is hoisted.

Widen the instruction record to 32 bytes and the extras row to four
8-byte fields, and store beside each serialized index the pointer it
resolves to: the constant object, the target record, the extras row,
the cache slot. Rows resolve their own fields the same way. Every pointer
is derived at finalize and on every load, exactly as `fn` is today, and
never serialized. A jump becomes a store and a tail call; a constant is
one load; a row is one load, and a constant inside a row is two.

## The load chains today

| datum | path | dependent loads |
|---|---|---|
| next instruction, every dispatch | `es->code`, `code->code`, `[pc].fn` | 3 |
| extras row (84 EXTRA opcodes) | `es->code`, `code->extras`, `[arg]` | 3 |
| constant at a CONST op | `es->code`, `code->cpool`, `[arg]` | 3, then the object |
| constant field inside a row | the row, then `es->code`, `cpool`, `[i]` | 6 |
| jump target | `es->code`, `code->code`, `+target` | 2 |
| cache slot | `es->code`, arithmetic, the slot | 1, then the slot |

The elector measurement in [../interpreter/electors.md](../interpreter/electors.md) put a number on
one of these: a CALL_FAST elector that re-read its row at entry ran
0.5% fewer instructions than the ladder it replaced and 1.3% more
cycles, the difference being the three-load walk to `nargs` at the head
of every elected call.

## The record

```c
typedef struct KosOp {                 /* 32 bytes, 32-aligned */
  KosThreadFn fn;                      /* derived: kos_op_handlers[op] */
  uint16_t op;
  uint16_t arg;                        /* serialized; the target pc for LABEL */
  uint16_t cache;                      /* serialized slot index, or KOS_CACHE_UNSET */
  uint16_t flags;                      /* reserved; zero until a bit is defined */
  union {                              /* derived; selected by style */
    PyObject *obj;                     /* CONST, VAR: the pool object, borrowed */
    KosOp *target;                     /* LABEL: the target record */
    KosExtra *row;                     /* EXTRA: the row */
  } u;
  KosCacheSlot *slot;                  /* derived; NULL when the site has no slot */
} KosOp;
```

The serialized half (`op`, `arg`, `cache`) is unchanged: `soften` emits
it, `.kio` stores it, introspection reads it. `flags` names the two
bytes that are padding today; it is zeroed at finalize and defines no
bits until the insn-flags proposal claims them. The derived half is the
resolved twin of `arg` and `cache`.

Thirty-two bytes, not twenty-four: a 24-byte record straddles a 64-byte
line one time in three, and the fetch reads `fn` at the front and the
operand at the back, so a straddling record costs two lines. At 32 the
array is two records per line and none straddles.

## The row

```c
typedef union KosExtraField {          /* 8 bytes, interpreted by the schema */
  uint32_t imm;                        /* COUNT, FASTVAR, OPCOUNT, CMP, CMPSRC, STAR, OPERAND */
  PyObject *obj;                       /* CONST, VAR: the pool object, borrowed */
  KosOp *target;                       /* LABEL: the target record */
} KosExtraField;

typedef struct KosExtra {              /* 32 bytes, one per EXTRA instruction */
  KosExtraField f[4];
} KosExtra;
```

Rows are appended one per EXTRA instruction at finalize
(`bytecode.c`, pass 2), never shared between sites. The schema for the
opcode says which member each field holds, as it says today how each
16-bit field is read. A row keeps no index form: `soften` recovers a
constant's index by identity against the cpool, which is
identity-deduplicated, and a label's pc by pointer arithmetic against
the block.

## What converts

By top-level style, 219 opcodes:

| style | ops | today | proposed |
|---|---|---|---|
| EXTRA | 84 | index into `code->extras` | `u.row` |
| CONST | 23 | cpool index | `u.obj` |
| LABEL | 11 | pc | `u.target`, with `arg` still the pc |
| VAR | 9 | cpool index of the name | `u.obj` |
| any cache-consuming op | 27 | slot index off `es->code` | `slot` |
| NOARG, COUNT, PAIR_COUNT, STAR, OPCOUNT, CMPCOUNT | 81 | immediate | immediate |
| FASTVAR | 9 | slot index | index: namespace-relative, not code-relative |
| COMPILE | 2 | pseudo-op | stripped before finalize |

Inside rows, by field kind: CONST (81) and VAR (39) become `obj`, LABEL
(9) becomes `target`, and COUNT, FASTVAR, OPCOUNT, CMP, CMPSRC, STAR,
and OPERAND stay immediates.

What stays an index everywhere: FASTVAR; the pc held by frames,
continuations, positions, and extents, which must survive relocation;
`arg` and `cache` on the record, as the serialized and introspected
form.

## The fetch and the jump

The record makes the companion change to the fetch possible, and the
jump's promise depends on it:

- `RESULT_NEXT`: `next = insn + 1`, then `TAILCALL(next->fn, es, next)`.
  No loads for the address, one for `fn`. `es->acting_pc` and `es->pc`
  are still stored, from `es->pc` itself.
- `RESULT_JUMP`: `es->pc = insn->arg`, then
  `TAILCALL(insn->u.target->fn, es, insn->u.target)`.
- A fused skip (the POP peephole) steps `insn + 2`.
- A frame install or revive threads from the new frame's code at its
  pc, as today, through the base the install just loaded.

`KosEvalState_OPS` stays for the places that thread from a pc they were
handed (a raise target, a continuation, a revive).

## Relocation

Pack copies a block into an arena. Two classes of pointer live in a
block:

- **Into the block** (`u.row`, `u.target`, a row's `target` fields):
  shifted by the pack delta, under the writable window where `fn` is
  already re-derived. The fill pass becomes a relocation pass.
- **Out of the block** (`u.obj`, a row's `obj` fields, `slot`): stable.
  The cpool keeps its objects alive and is not mutated after finalize;
  the `KosCode` object, which carries the slot array, never moves.

The invariant that makes this sound already governs pack: it runs at a
quiescent point, with the module body complete and no frame of the
target code mid-dispatch. Nothing that outlives that point may hold a
pointer into a block. Frames, continuations, defers, and parked frames
hold pc indices, as they do now; a handler holds `insn` only for its
own execution. This is written into
[../interpreter/execution-invariants.md](../interpreter/execution-invariants.md) §20.

## Serialization and tooling

Nothing derived is serialized. `.kio` is unchanged. `soften` and the
`extra_tuple` getter keep producing the index view, so `dis.ki`,
`opgrams`, and the kio loader see what they see today. `KosOp` and
`KosExtra` change layout, so the ABI version moves.

Validation stays on indices at finalize, where a bad one raises. A
pointer derived from a validated index cannot be bad; under
`KOS_EVAL_SAFETYNET` the fill pass range-checks every derived pointer
against the block anyway, so a defect there reports instead of
faulting inside a sealed page.

## Consequences for electors

A cache-style elector reads its site through two loads that the fetch
already warmed: `insn->u.row` for the site's constants and `insn->slot`
for the guard and the body. The row walk that cost the CALL_FAST elector
its cycles is gone, and the pass-through contract in
[../interpreter/electors.md](../interpreter/electors.md) stands without amendment: `value` is the
guarded operand, and everything else is one load off the record.

## Order of work

1. Widen `KosOp` to 32 bytes with the derived union and slot pointer,
   fill them at finalize and on load, relocate on pack. Handlers read
   the new fields through the existing accessor macros, which change
   underneath them.
2. Widen `KosExtra` to resolved fields; `KosCode_GetExtra` returns the
   row pointer; the row accessors read `imm`, `obj`, or `target`.
3. Change the fetch and the jump.
4. Measure each step with `perf stat` over the churn kernels, as the
   elector was measured.
5. Return to the elector with the row walk gone.

# The end.
