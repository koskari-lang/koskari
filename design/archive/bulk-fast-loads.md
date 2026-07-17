# Bulk Local Loads: VAR2 / VAR4 and FAST2 / FAST4

**Status: COMPLETE (2026-07-17, branch `var4`). All four opcodes,
the `pack_var_runs` late sweep (with mid-run POSITION absorption --
positions are interleaved in the stream and would otherwise break
every run), and the automatic twin lowering are implemented. Suite
4712 passed / 10 skipped. Gate metric met exactly: FAST runs >= 2
vanished from the whole corpus (345 singletons remain; 119 FAST2 +
5 FAST4 emitted across bootstrap/life/churn). Churn: loop -2.9%
(16.87s best), tree/method within noise; the len-2 population's
remaining upside transfers to compare-unification Phase 2 as
sequenced below. Implemented ahead of compare-unification by
deliberate choice (low-hanging fruit); the packer ran first, so
compare fusion will take its loop-condition feeders back from
FAST2 rows rather than raw runs -- re-baseline there, as planned.**

## Purpose

Runs of consecutive `FAST` loads are the single most common instruction
sequence the compiler emits: `FAST FAST` is the top weighted 2-gram in
the static corpus, `FAST FAST FAST` the top 3-gram, and FAST's most
common successor is another FAST. The dominant producer is call
argument gathering -- positional calls whose arguments are just named
locals, where collecting a set of names onto the stack is the entire
point of the sequence. A dis of realistic ported-Python code shows
chains of 2, 3, and up to 8 `FAST` rows feeding a single call.

This adds four opcodes -- `VAR2`/`VAR4` and their lowered twins
`FAST2`/`FAST4` -- that push two or four bindings in one dispatch.
No call-shape opcodes, no consumer knowledge: just loading the stack
from the namespace in fewer dispatches, which helps every consumer
(calls, BUILD_ARGS, string building) equally.

## Evidence

FAST run-length distribution over the opgrams corpus (84 codes:
bootstrap, cli, koslife, churn; 2026-07-16), runs measured within
straight-line blocks:

| run length | raw count | loop-weighted (base 100) |
|---|---|---|
| 1 | 455 | 2.33M |
| 2 | 125 | 3.30M |
| 3 | 17 | 1.02M |
| 4 | 6 | 2.01M |

Greedy repacking against the 20.0M weighted baseline of FAST
dispatches:

- `FAST4 + FAST2`: **-52%**
- `FAST2` only: -42%
- `FAST4 + FAST3 + FAST2`: -57% (the extra 5 points rest on 17 raw
  sites -- FAST3 is not worth a fifth opcode; greedy `FAST2 + FAST`
  covers length-3 runs in two dispatches)

The in-tree corpus contains no runs longer than 4; longer chains seen
in ported code (the 8-argument call above) pack as `FAST4 FAST4`.

## Encoding

All four opcodes are EXTRA rows. **No packed-argument variant in this
design**: uniform EXTRA keeps one encoding, one decode shape, and slot
kinds that the schema states fully. If measurement later shows the
2-element row fetch is too slow, a shifted-space direct-arg encoding
for FAST2 (two 7-bit indices; `FAST_SLOT_MAX` is 128) is the recorded
escape hatch -- see Alternatives.

```
OP(VAR2,  0, EXTRA, VAR, VAR, NOARG, NOARG)
OP(VAR4,  0, EXTRA, VAR, VAR, VAR, VAR)
OP(FAST2, 0, EXTRA, FASTVAR, FASTVAR, NOARG, NOARG)
OP(FAST4, 0, EXTRA, FASTVAR, FASTVAR, FASTVAR, FASTVAR)
```

Every slot kind is schema-static. That means **zero new logic** in the
generic machinery: `convert_fast`'s EXTRA lowering, `soften` /
hardening, dis rendering, `collect_label_map`, and the codex
serializer all handle these rows from metadata they already read. (No
CMPSRC-style data-dependent slots anywhere in this design.)

Stack effect: +2 / +4. No cache slot use (`KOS_CACHE_UNSET`, same as
FAST).

## Handler behavior

- One stack-reserve check for the whole group instead of one per push.
- Resolve all N values first, then push: on a resolution failure
  (unbound name in the VAR forms, empty fast slot in the FAST forms)
  nothing has been pushed, cleanup is decref of the already-resolved
  temporaries, and the error is exactly the error the equivalent
  single op would have raised for that name.
- `FAST2` / `FAST4`: hot (`KOS_OP_HANDLER`), fully inline loads --
  these are the loop-body and call-path forms. Cost target: barely
  above one FAST.
- `VAR2` / `VAR4`: **cold** (`KOS_OP_HANDLER_COLD`) from their first
  commit, per the standing VAR rationale (a97c6909): VAR forms survive
  fast-conversion only in one-shot expression-level code.

## Compiler: emission and lowering

The single-lowering-path property is the design's second point (after
dispatch count): there is exactly one place that knows how VAR becomes
FAST today, and this feature keeps it that way.

### Emission: greedy VAR-chain packing

A peephole watches for chained `VAR` rows and combines them greedily
in the order **VAR4, VAR2, VAR** (an 8-chain becomes `VAR4 VAR4`; a
3-chain becomes `VAR2 VAR`; a 5-chain `VAR4 VAR`). Constraints:

- Only plain `VAR` rows combine -- not VAR_ATTR, not VAR_REF, nothing
  with side structure.
- Runs never cross a LABEL or a block-ending opcode (the same
  straight-line rule opgrams counts by).
- **The packer runs after consumer-specific fusions.** Compare-feeder
  fusion (compare-unification Phase 2) claims its `VAR`/`CONST`
  feeders during the main compress() look-back; if chain packing ran
  first, a merged `VAR2` would hide the feeders from the better
  fusion. Chain packing is therefore a discrete late sweep over the
  squished stream (end of `compress()` or a separate pass before
  `normalize_opcount`), mopping up the runs no consumer fusion
  claimed. This ordering is load-bearing; it is what makes the two
  designs compose instead of compete.

### Lowering: the existing path, untouched

`_build_var_to_fast()` pairs twins by name substitution, so
`VAR2 -> FAST2` and `VAR4 -> FAST4` register automatically. The
generic EXTRA lowering applies: each KOSTYLE_VAR slot resolves through
`fast_slot()`, and the existing **all-or-nothing rule** holds -- if
any name in the row lacks a fast slot (not in the pool, or beyond
`FAST_SLOT_MAX`), the whole row stays VAR-form. No splitting a VAR4
back into pieces to salvage three lowerable names: mixed-eligibility
rows are rare, and they land on the cold VAR handler where their cost
is irrelevant.

### dis

Free. The FASTVAR slot renderer already resolves slot indices to
local/closure names, and the VAR slot renderer already shows pool
symbols; a `FAST4` row renders as four named loads on one line, which
is strictly easier to read than the four rows it replaces.

## Sequencing and measurement

The length-2 population overlaps with compare fusion: loop conditions
(`FAST FAST CHECK_LT`) contribute weighted len-2 runs that
`CHECK_COMPARE_FAST` will absorb entirely. Building bulk loads first
would claim credit compare fusion then takes back.

Therefore: **implement and measure after compare-unification Phase 2**,
against the post-compare corpus (regenerate the run-length table with
opgrams; the packer's honest payoff is what remains -- dominated by
call-argument gathering, which compare fusion does not touch).

Phases, gated like the companion doc:

**Phase 0 -- baseline.** Post-compare-fusion opgrams run-length table
and churn numbers.

**Phase 1 -- the four opcodes.** opcodes.incl entries, handlers (VAR
twins cold), the late-sweep packer, lowering (automatic), dis check,
tests (including the unbound-name error path through a bulk row and a
mixed-eligibility row staying VAR). ABI note: adds four opcodes --
ordinal renumbering downstream of the insertion point, `.kio`
regeneration, Python OpCode mirror; same migration class as every
opcode-set change, and worth co-scheduling with compare-unification's
renumbering so the ABI churn lands once.
*Gate*: full suite; method/call-heavy churn pairs (method, list_each)
and any call-dense benchmark should move; verify via opgrams that FAST
runs >= 2 vanish from the corpus (the packer's own success metric is
its input pattern disappearing).

## Alternatives considered

- **FAST3.** Rejected on the data: 17 raw sites, and greedy
  `FAST2 + FAST` already covers length-3 in two dispatches. The
  marginal 5 weighted points do not buy a fifth opcode.
- **Packed direct-arg FAST2** (two 7-bit indices in the uint16 arg, no
  extra row). Deferred, not rejected: uniform EXTRA encoding first; if
  FAST2 shows up too slow under measurement, the shifted-space
  encoding is the recorded next step. Revisit only with numbers.
- **FAST-only (no VAR twins), packing after convert_fast.** Rejected:
  it forks the lowering story -- today VAR-to-FAST happens in exactly
  one place, and a post-lowering packer would be a second,
  FAST-specific transformation pass. The VAR twins cost two cold
  handlers and keep emission (compress) and lowering (convert_fast)
  in their existing roles.
- **Mixed-source bulk loads** (FAST/CONST interleaved in one row).
  Deferred to the operand-fusion family; this design deliberately
  knows nothing about what consumes the values.
- **Simple call forms** (call opcodes carrying argument slot lists).
  Deferred in favor of bulk loads: loading the stack in fewer
  dispatches helps calls *and* every other consumer, adds no coupling
  to the call core, and costs four opcodes against a whole family.

<!-- The end. -->
