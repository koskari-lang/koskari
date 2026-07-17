# Comparison Unification: CHECK_COMPARE, OBJECT_COMPARE, and Fused Source Variants

**Status: Phase 1 COMPLETE (2026-07-17, branch `check_cmp`). The 24
comparison opcodes are deleted; CHECK_COMPARE / OBJECT_COMPARE carry
the whole surface on the KosCompare selector. Suite 4725/10 green.
Dispatch fn 66,045 -> 63,579 B (-3.7%). Gate note: the first
measurement showed method +9.5% -- the chain core had become an
out-of-line cross-TU call where kos_check_cmp used to inline; moving
the richcompare-six fast path into operator.h as static inline (cold
extern tail for the extended selectors) recovered it to +6%, with
loop (4x the CHECK_LT volume) at only +1.4% -- the differential
pins the residual on method's documented layout sensitivity, and P2
replaces this exact sequence at loop heads anyway. Phases 2-3
pending.**

## Purpose

The comparison surface is 24 opcodes that all do one of two things:
reduce a run of operands under a comparison and either **branch** on
the result (the `CHECK_*` family) or **push a bool** (the `OBJECT_*`
comparison family). Every one of them already funnels into a shared
core parameterized by the comparison (`kos_check_cmp(es, insn, Py_LT)`,
`KosOperator_Lt(...)`); the opcode identity carries no information the
comparison selector doesn't.

This document specifies collapsing the 24 into two selector-keyed
opcodes, then adding fused operand-sourcing variants that the flat
encoding makes affordable. Two goals, same shape as
[call-unification.md](call-unification.md):

1. **Shrink**: 24 opcodes become 2 generic + 4 fused (net -18), with
   the VAR-form fused twins cold from day one.
2. **Accelerate**: the fused forms collapse the dominant loop-condition
   pattern (`FAST FAST CHECK_LT`: 3 dispatches) to 1, and the bounds
   idiom (`CONST FAST CONST OBJECT_LT JUMP_FALSE`: 5 dispatches) to 2.

Without the selector enum, fusing checks means one opcode per
(comparison x operand-shape) pair -- a dozen new opcodes for CHECK
alone, worse with each future family. With it, the entire fused-check
surface is capped at four opcodes forever. That containment, not the
handler dedup, is the strategic payoff.

Non-goals: the SWITCH_EQ family (already selector-specialized in a
different dimension); `CHECK_NIL` (unary, already a squashed
NILP+JUMP_FALSE); `CHECK_EXCEPTION` (handler-entry semantics);
arithmetic operand fusion (separate effort, same evidence base).

## Evidence

From the 2026-07-16 trace (`trace.json`, full churn suite, 57.3B
dispatches) and the opgrams static corpus (84 codes: bootstrap, cli,
koslife, churn):

- `CHECK_LT` runs 1.31B times (23 ns avg), `CHECK_EQ` 510M, `CHECK_LE`
  165M; the CHECK family totals ~48B ns, ~3.4% of traced opcode time.
- `PUSH_LOOP FAST FAST CHECK_LT` is the **top weighted 4-gram** in the
  static corpus; `FAST FAST CHECK_LT` is the top condition 3-gram.
  Every loop head in the suite is this shape.
- Nearly all `FAST`/`CONST` executions exist to feed consumers
  (operand pairs, checks, item indices); each fused check removes two
  of those feeder dispatches (~38-42 ns traced) per evaluation.
- The bounds idiom appears statically as the literal 4-gram
  `CONST FAST CONST OBJECT_LT` (+`JUMP_FALSE`).

Refresh both (tox -qe churn-trace; `uv run python tools/opgrams.py`)
for Phase 0 baselines at implementation time.

## Current surface (all 24 to be deleted)

| opcodes | style | arity | core |
|---|---|---|---|
| CHECK_EQ/GE/GT/LE/LT/NE | EXTRA (COUNT, LABEL) | chain, COUNT >= 2 | `kos_check_cmp` (RichCompareBool chain) |
| CHECK_IN/NOT_IN | LABEL | 2 | `kos_check_two_arg` |
| CHECK_IS/IS_NOT | LABEL | 2 | `kos_check_identity` |
| CHECK_IS_INSTANCE/IS_SUBCLASS | LABEL | 2 | `kos_check_two_arg` |
| OBJECT_EQ/GE/GT/LE/LT/NE | COUNT | chain, COUNT >= 2 | `KosOperator_Eq` .. `KosOperator_Ne` |
| OBJECT_IN/NOT_IN/IS/IS_NOT | NOARG | 2 | per-op body |
| OBJECT_IS_INSTANCE/IS_SUBCLASS | COUNT | COUNT form | per-op body |

Retained unchanged: `CHECK_NIL`, `CHECK_EXCEPTION`, `NILP`,
`OBJECT_NOT`, the SWITCH_EQ family.

**Semantics rule for the whole effort: the per-selector cores are
preserved verbatim.** The enum only routes. Whatever
`OBJECT_IS_INSTANCE` with COUNT > 2 means today is what
`OBJECT_COMPARE (IS_INSTANCE, n)` means tomorrow. Arity legality
(which selectors accept chains, which are strictly binary) is enforced
where it is enforced today: at emission, in the specials.

## The KosCompare selector

```c
/* WARNING: values 0..5 are CPython's Py_LT..Py_GE ordinals, relied on
   for passthrough into PyObject_RichCompareBool. All ordinals are
   bytecode ABI (mirrored by the Python Compare IntEnum, encoded in
   CMP/CMPCOUNT/CMPSRC fields); keep this order stable. */
typedef enum KosCompare {
  KOS_COMPARE_LT = 0,          /* == Py_LT */
  KOS_COMPARE_LE = 1,          /* == Py_LE */
  KOS_COMPARE_EQ = 2,          /* == Py_EQ */
  KOS_COMPARE_NE = 3,          /* == Py_NE */
  KOS_COMPARE_GT = 4,          /* == Py_GT */
  KOS_COMPARE_GE = 5,          /* == Py_GE */
  KOS_COMPARE_IS,
  KOS_COMPARE_IS_NOT,
  KOS_COMPARE_IN,
  KOS_COMPARE_NOT_IN,
  KOS_COMPARE_IS_INSTANCE,
  KOS_COMPARE_IS_SUBCLASS,
  KOS_COMPARE_COUNT           /* number of selectors; invalid sentinel */
} KosCompare;
```

Twelve selectors fit 4 bits with four values spare. Dispatch inside the
cores is the operator.c single-branch pattern (f985cd83): one likely
branch for `sel <= KOS_COMPARE_GE` straight into the RichCompareBool
chain (the selector passes through untranslated), a switch for the
extended six. Python side gains a `Compare` IntEnum mirroring the
ordinals, next to `Operand`.

Two core entry points, living in operator.c beside
`kos_operand_reduce`:

```c
/* chain-evaluate; 1 = held, 0 = failed, -1 = error */
int kos_compare_chain(KosCompare sel, PyObject *const *args, size_t count);

/* same reduction, returning a new-ref bool (or richer result if a
   selector's existing core returns one) */
PyObject *kos_compare_reduce(KosCompare sel, PyObject *const *args, size_t count);
```

The CHECK forms call the first and branch; the OBJECT forms call the
second and push. The twelve deleted CHECK handler files and twelve
OBJECT bodies collapse into these two functions plus six thin handlers.

## New field kinds: CMP, CMPCOUNT, CMPSRC

(The codebase uses kind/type/style interchangeably for these; they are
all "how to treat the value encoded in the direct argument or an extra
slot." No lexical distinction is intended here either.)

- **CMP** -- a bare KosCompare selector. Appears as extra slot a0 in
  `CHECK_COMPARE` and the ternary OBJECT fused pair.
- **CMPCOUNT** -- selector in the high byte, reduce count in the low
  byte, packed into a direct argument. The CMP analogue of OPCOUNT,
  named distinctly so dis and readers know which enum decodes the high
  byte. Gets compose/decode macros beside `KOS_OPCOUNT`'s, and the
  compiler-side merge in `normalize_opcount` generalizes to cover it
  (the same (selector, count) tuple-merge treatment).
- **CMPSRC** -- selector plus per-operand source-shape bits, packed
  into extra slot a0. **Used only by the arity-2 CHECK fused pair.**

CMPSRC layout:

```
bit  15..6   reserved, must be zero
bit  5       source kind of operand y   (0 = primary, 1 = constant)
bit  4       source kind of operand x   (0 = primary, 1 = constant)
bits 3..0    KosCompare selector
```

"Primary" means the opcode's namesake source: a var-name pool index in
`CHECK_COMPARE_VAR`, a fast slot index in `CHECK_COMPARE_FAST`.
"Constant" means a constant pool index in both. The shape bits are
identical across the VAR/FAST twins -- that is what makes lowering an
opcode swap (see below).

## New opcode surface

| opcode | encoding | temp | role |
|---|---|---|---|
| CHECK_COMPARE | EXTRA (CMP, COUNT, LABEL, NOARG) | hot | generic reducing branch; stack operands; branch on chain failure |
| OBJECT_COMPARE | CMPCOUNT direct arg | hot | generic reducing bool push; stack operands |
| CHECK_COMPARE_VAR | EXTRA (CMPSRC, raw, raw, LABEL) | **cold** | arity-2 branch; operands from VAR/CONST per shape bits |
| CHECK_COMPARE_FAST | EXTRA (CMPSRC, raw, raw, LABEL) | hot | arity-2 branch; operands from FAST/CONST per shape bits |
| OBJECT_COMPARE_VAR | EXTRA (CMP, CONST, VAR, CONST) | **cold** | ternary bounds bool; fixed const/var/const shape |
| OBJECT_COMPARE_FAST | EXTRA (CMP, CONST, FASTVAR, CONST) | hot | ternary bounds bool; fixed const/fast/const shape |

Notes:

- The generic forms are hot but thin, per the call-unification handler
  discipline: inline arg decode, one call into the shared core. They
  remain the fallback for every unfused emission (arity > 2 chains,
  stack-fed operands), so they cannot be cold.
- The VAR twins are `KOS_OP_HANDLER_COLD` **from their first commit**,
  by the same rationale as the 2026-07-16 VAR sweep (a97c6909): VAR
  forms survive fast-conversion only in one-shot expression-level code.
  Carrying the twin costs the dispatch loop essentially nothing.
- The ternary pair uses **pure CMP and schema-static slot kinds**
  (CONST/VAR/CONST). This is deliberate: every generic consumer of
  extra schemas -- `convert_fast`'s EXTRA lowering, `soften`, dis, the
  codex serializer -- handles these rows with zero new logic. CMPSRC's
  data-dependent slots are confined to exactly two opcodes.
- The fused CHECK pair's operand slots are declared with a neutral raw
  kind in `opcodes.incl` (COUNT serves; the values are indices whose
  interpretation lives in the CMPSRC bits). Every consumer that needs
  true kinds derives them from a0. This is the one intra-row
  dependency in the design; each site that must know about it is
  enumerated in "CMPSRC-aware sites" below.
- LABEL sits at a schema-declared position in both CHECK forms (a2
  generic, a3 fused), so `collect_label_map`, opgrams' block-ender
  detection, and jump retargeting all pick the new opcodes up from
  metadata without special cases.
- There is **no arity-2 OBJECT fused form** in this effort. Value-
  position binary compares fall to generic `OBJECT_COMPARE`; the
  opgrams tables show branch position is where the volume is. Revisit
  only with data.

### Why the ternary is a bool producer, not a branch

`(< 0 x 100)` in branch position wants (CMP, CONST, FAST, CONST,
LABEL) -- five fields, one more than a row holds. Splitting it as
bool-producing ternary + `JUMP_FALSE`/`JUMP_TRUE` costs one extra
predictable dispatch and fits flat. Today the idiom costs five
dispatches; fused it costs two. The five-field row is representable
via chained extras (see Alternatives) but pays a dependent row fetch
on every execution to save one jump dispatch -- the wrong trade on a
hot path.

## Compiler pipeline: where compression and lowering happen

The pass order in `Compiler.complete` is fixed and load-bearing:

```
compress()  ->  normalize_opcount()  ->  convert_fast()  ->  demote_tail_calls_if_yields()
```

**Compression happens before fast conversion.** The specials and the
peephole see VAR-form feeders; fast slots do not exist yet. Therefore
all fusion emits the `_VAR` fused forms, and the FAST forms are only
ever produced by `convert_fast` lowering. This ordering is why the
VAR/FAST twin pattern exists at all, and the design leans into it
rather than fighting it.

### Phase 1 emission changes (behavior-neutral)

- `specials/object.py`: `_chain_compare` / `_binary_op` /
  `_type_check` stop selecting among twelve opcodes and emit
  `OBJECT_COMPARE` with a (selector, count) CMPCOUNT argument.
- `compress()`: `_CHECK_SQUASHES` and `_CHECK_VAR_SQUASHES` merge into
  one map keyed on `OBJECT_COMPARE`: prior `OBJECT_COMPARE (sel, n)` +
  `JUMP_FALSE L` squashes to `CHECK_COMPARE (sel, n, L)`. The
  NILP/EXCEPTION_MATCH/OBJECT_NOT squash entries survive unchanged.

### Phase 2: the fused peephole (in compress, after the CHECK squash)

Pattern, matched against the tail of the squished stream exactly like
the existing squashes (stack discipline guarantees adjacency means
feeding):

```
VAR x | CONST a           }  two feeder rows, each VAR or CONST
VAR y | CONST b           }
CHECK_COMPARE (sel, 2, L)
  ==>  CHECK_COMPARE_VAR ((sel|shape), x', y', L)
```

Guards:

- count must be exactly 2; chains stay generic.
- Both feeders must be plain `VAR`/`CONST` rows (not VAR_ATTR, not
  DUP'd, not separated by LABEL -- look-back on `squished[-1]`/
  `squished[-2]` enforces adjacency for free, same as every existing
  squash).
- Both-CONST is left to constant folding, not fusion (a compare of two
  constants is a compile-time bool; fold it or leave it generic --
  either way it is not a fusion target).
- **JUMP_TRUE does not fuse via selector inversion.** `not (a < b)` is
  not `a >= b` under partial orders (NaN), so the inversion is
  semantically wrong in general. JUMP_TRUE consumers keep the generic
  bool + jump shape. (See Alternatives for the rested branch-sense
  bit.)

And the ternary pattern:

```
CONST a
VAR x | (any single primary feeder)
CONST b
OBJECT_COMPARE (sel, 3)
  ==>  OBJECT_COMPARE_VAR (sel, a, x, b)
```

emitted only on the exact const/primary/const shape -- the bounds
idiom. Other ternary shapes stay generic until opgrams shows volume.

### Phase 2: convert_fast lowering

`_build_var_to_fast()` pairs FAST/VAR twins by name substitution, so
`CHECK_COMPARE_VAR -> CHECK_COMPARE_FAST` and `OBJECT_COMPARE_VAR ->
OBJECT_COMPARE_FAST` register automatically -- provided the names keep
the `FAST`/`VAR` token, which they do.

- `OBJECT_COMPARE_VAR` lowers through the **existing generic EXTRA
  path** untouched: its schema statically marks slot a2 as
  KOSTYLE_VAR, `fast_slot()` resolves it, the all-or-nothing rule
  applies as today.
- `CHECK_COMPARE_VAR` needs the one special case of this design:
  slot kinds come from the CMPSRC bits, not the schema. The lowering
  reads a0, attempts `fast_slot()` on each primary-bit operand, and
  applies the same all-or-nothing rule: if any primary operand lacks a
  fast slot (not in the pool, or beyond `NAMESPACE_FAST_SLOT_MAX`),
  the whole row stays VAR. Constant-bit operands and the CMPSRC word
  itself pass through untouched. The shape bits are identical in both
  forms, so lowering is: swap opcode, remap primary-bit indices, done.

### CMPSRC-aware sites (the complete list)

Because the fused CHECK slots are data-dependent, each consumer that
interprets extra slots by kind needs a CMPSRC pre-step. This is the
full set; anything not listed here works from schema alone:

| site | what it must do |
|---|---|
| `convert_fast` (compiler.py) | read shape bits; lower primary-bit slots; all-or-nothing |
| `code_collect` / hardening (bytecode.c) | pool constant-bit operands and (VAR form) name operands per bits |
| `soften_extra_tuple` (bytecode.c) | reverse of the above: resolve constant-bit slots to pool values, name-bit slots to symbols |
| `dis._format_arg` (dis.py) | render the row as e.g. `LT fast 0 ('x'), c#3 (100) -> L2` -- selector name, per-operand source and resolved value, target |
| corpus / compile-phase verify | inherits readable dis output; no bespoke logic |

The evaluator itself does not "interpret kinds"; the handler knows its
own encoding. `CHECK_COMPARE_FAST`'s body dispatches the 2 shape bits
through a 4-way switch where each arm has fixed load kinds (fast/fast,
fast/const, const/fast, const/const) -- monomorphic per arm, the
single-branch style of operator.c, rather than per-operand
data-dependent selects shared across all sites.

### dis augmentation (all three kinds)

dis renders from the OpStyle metadata tables, so the additions are
mechanical but should land with Phase 1/2, not after:

- **CMP** slot: selector name (`LT`, `IS_INSTANCE`), from the Compare
  IntEnum.
- **CMPCOUNT** arg: `LT x3` (selector, then count), mirroring how
  OPCOUNT args render today.
- **CMPSRC** row: as in the table above -- decode selector + shapes,
  then reuse the existing FASTVAR/CONST slot renderers per bit, so
  fast slots still resolve to local/closure names and constants to
  pool reprs.

`Code.soften()` and dis are also what the corpus records and
compile-phase tests read; getting rendering right in the same commit
as each opcode keeps that surface verifiable throughout.

## Handler and core placement

- `koskari/lib/opcode/CHECK_COMPARE.c`, `OBJECT_COMPARE.c`,
  `CHECK_COMPARE_VAR.c` (cold), `CHECK_COMPARE_FAST.c`,
  `OBJECT_COMPARE_VAR.c` (cold), `OBJECT_COMPARE_FAST.c`. Twelve
  CHECK_*.c and the OBJECT comparison bodies are deleted.
- `kos_compare_chain` / `kos_compare_reduce` in operator.c, beside
  `kos_operand_reduce`, sharing its layout: per-selector cores with no
  runtime export, reached only through the two switches. The existing
  helpers (`kos_check_cmp`'s chain loop, `kos_check_identity`,
  `kos_check_two_arg`'s targets, `KosOperator_Lt` et al.) relocate or
  are absorbed; eval.c sheds the check helpers entirely.
- Follow-up candidate (not this effort): a 2-operand PyLong compare
  fast path in the chain core, mirroring the longops arithmetic fast
  path. Measure Phase 2 first; RichCompareBool already has the
  identity shortcut for EQ/NE.

## ABI and migration mechanics

Deleting 24 opcodes renumbers everything after `CHECK_EQ`'s slot.
Ordinals are bytecode ABI (the bytecode.h warning applies), so:

- The Python `OpCode` IntEnum and everything generated from
  `opcodes.incl` (handlers.incl via `tox -e handlers`) regenerate.
- Compiled `.kio` artifacts are invalidated and regenerate; treat this
  as the same class of event as the KosOperand introduction
  ([kio-format.md](kio-format.md) governs whether a format version
  bump is warranted).
- Corpus records that verify at the compile phase (disassembly
  expectations) need a re-verification sweep; the layout/reader
  phases are untouched.
- Tests asserting opcode names: tests/compile and tests/lib dis tests
  get a mechanical update; reproducer tests should be unaffected
  (behavior-neutral phases) except where they disassemble.
- The bootstrap recompiles at load; no action.
- opgrams and the churn harness need nothing: both are metadata-driven.

## Phases

**Phase 0 -- baseline.** Regenerate trace.json (tox -qe churn-trace),
opgrams tables, and [dispatch-sizing.md](../dispatch-sizing.md)
numbers on the branch point. (The 2026-07-16 captures qualify if the
branch starts from this master.)

**Phase 1 -- compression, behavior-neutral.** KosCompare + Compare
IntEnum; CMP and CMPCOUNT kinds; `kos_compare_chain` /
`kos_compare_reduce` in operator.c; CHECK_COMPARE + OBJECT_COMPARE
opcodes and handlers; specials emission; compress() squash-map merge;
delete the 24; dis/soften/mirror/test/corpus migration.
*Gate*: full suite green; churn within noise of baseline (the selector
becomes a runtime load where it was a baked constant -- the budget for
that is the icache shrink; measure, don't assume); dispatch-sizing
regen showing the surface reduction.

**Phase 2 -- fused CHECK pair.** CMPSRC kind; CHECK_COMPARE_VAR
(cold) / CHECK_COMPARE_FAST; fused peephole in compress(); the
convert_fast CMPSRC special case; dis/soften CMPSRC rendering.
*Gate*: full suite + ASAN; churn loop/tree/branch all contain fusable
loop conditions and should move visibly (every `FAST FAST CHECK_*`
loop head drops two dispatches per iteration); confirm via dis of the
churn kernels (opgrams --dis) that loop heads emit CHECK_COMPARE_FAST.

**Phase 3 -- ternary bounds pair + closing.** OBJECT_COMPARE_VAR
(cold) / OBJECT_COMPARE_FAST; bounds pattern in compress(); sizing
regen; record outcomes here; corpus examples for the new disassembly
shapes.
*Gate*: full suite; a bounds-check microbenchmark or corpus reproducer
demonstrating 5 -> 2 dispatches; final numbers recorded in this
document.

Each phase is a commit (or small stack) gated on review, matching the
call-unification cadence. Do not start a phase with the prior phase's
gate unmeasured.

## Alternatives considered

- **Named shape variants** (`CHECK_COMPARE_FAST_FAST`,
  `_FAST_CONST`, `_CONST_FAST`, x VAR twins = six opcodes minimum,
  before the OBJECT side). Rejected: the opcode count this design
  exists to contain. CMPSRC caps the fused CHECK surface at two
  opcodes with all shapes expressible; the per-dispatch cost is one
  4-way switch on already-loaded bits.
- **CMPSRC on the ternary pair.** Rejected: fixed const/primary/const
  shape keeps the slots schema-static, which keeps convert_fast,
  soften, dis, and the serializer generic. Eight shape arms for a
  form dominated by one idiom is complexity with no measured demand.
- **Selector inversion to fuse JUMP_TRUE** (`not (a < b)` -> GE).
  Rested: wrong under partial orders (NaN). Would need per-type
  knowledge the compiler doesn't have.
- **Branch-sense bit in CMPSRC** (branch-on-true vs branch-on-false).
  Rested: encoding room exists (bit 6), but JUMP_FALSE dominates
  emission today and the generic path serves the rest. Revisit only if
  opgrams shows `OBJECT_COMPARE JUMP_TRUE` volume.
- **Five-field bounds branch via chained extras.** An extra slot can,
  in principle, hold the index of another extra row -- rows are
  bump-allocated at assembly, immutable behind the code seal, so
  chaining (even trees/DAGs of rows, with interned shared subtrees)
  is structurally sound, and the discipline would be that the
  *schema*, not the data, marks child-reference slots so soften/dis/
  serializer walk statically. Not used here: a dependent row fetch on
  every execution to save one predictable JUMP dispatch is the wrong
  trade. Recorded so the option isn't relitigated; it remains the
  growth path if a future fused form genuinely outruns four slots,
  best reserved for cold metadata.
- **Arity-2 OBJECT fused form** (value-position `(< a b)` with
  sourced operands). Deferred with a data bar: fuse it when opgrams
  shows the pattern warm. The encoding is trivially available (CMPSRC
  row, no label) if justified.

## Rested ideas

- PyLong 2-operand compare fast path in `kos_compare_chain` (longops
  analogue) -- measure after Phase 2.
- Generalizing CMPSRC-style source bits to other 2-operand consumers
  (ITEM's fannkuch traffic in opgrams is the candidate corpus) --
  belongs to the operand-fusion effort, not this one, but the field
  kind and its five aware sites are designed to be reusable.

<!-- The end. -->
