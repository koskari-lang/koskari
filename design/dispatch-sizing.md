# Dispatch function sizing baseline

Internal analysis for the effort to shrink `koskari_eval_dispatch_computed_goto`,
the single computed-goto dispatch function into which every opcode handler is
`always_inline`d.  As the opcode set grows, each handler's compiled size is
additive I-cache cost in that one function.  Goal: keep the hottest short paths
inline, migrate everything else one or two calls out.

## Methodology

Per-handler sizes come from a measurement build with `KOS_ALL_COLD=1`
(the hints.h gate un-inlines every handler), so each `op_<NAME>` becomes
its own symbol; sizes read via `nm --print-size` on the built extension.  Standalone noinline
size is a proxy for a handler's inlined contribution (inlining can specialise or
shrink it) -- good enough to rank and prioritise.  The proxy still over-estimates:
the fully-inlined dispatch fn (64,715 B) is smaller than floor + hot bodies
(65,627 B), so at `-O2 -flto` inlining shares tails and specialises away more
than the standalone bodies imply.  Shared static cores called from several
handlers sit outside the `op_` symbols and are not in the table (e.g.
`object_next_step`, the 1,367 B step core behind the three cold NEXT forms --
which is why OBJECT_NEXT reads far smaller than the old monolithic NEXT).
The operator cores in `operator.c` (`op_add`, `op_bit_and`, ...) share the
`op_` prefix but are not handlers; they are excluded from every figure here.
88 handlers are `KOS_OP_HANDLER_COLD` -- their bodies appear in the table below
but do not contribute to the inlined function at all.

Hotness is now backed by measurement for one workload: the
`KOS_TRACE_TIMING` build records per-opcode counts, and the churn
kernels are traced into `design/trace/`.  That covers the numeric-kernel
half of the picture only -- see the execution profile below for what
churn does and does not reach.  There is still no PGO/instrumented
build for layout, and no trace of the library-shaped workloads
(scheduler, mcp, repl, dis) whose opcode mix the emission view shows
is quite different.

Toggling `KOS_ALL_COLD` changes no source, and the Makefile's rebuild
guard is a content hash over `koskari/lib`, so the flag alone will not
trigger a rebuild.  Clear the build first:

    make clean-built && rm -f .venv/.koskari-srchash
    KOS_ALL_COLD=1 make venv

Measured 2026-08-20 on `master` at 66c2d8b6, 215 opcodes (one handler
each; 216 symbols counting one split-out `.cold`).

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 65,047 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 215 handlers un-inlined) | 26,866 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 215 + loop scaffold |
| handler bodies | 100,451 | sum of standalone noinline bodies -- 61,253 B across the 88 cold handlers, 39,198 B across the 127 hot ones (the actual inline budget) |

Distribution (top-heavy, long-tailed; % of the 100,451 B body):

- `>= 1000 B`: 24 handlers, 33,984 B (34% of body)
- `>=  500 B`: 62 handlers, 59,654 B (59% of body)
- `>=  200 B`: 169 handlers, 94,908 B (94% of body)
- `>=  100 B`: 198 handlers, 99,482 B (99% of body)

The floor grew 26,738 -> 26,866 B against the previous sweep: +128 B
is one opcode's worth of glue, matching CONST2.

## Applied changes since the previous sweep

**CONST2 bulk constant load (hot).**  One new opcode pairs adjacent
constant pushes, packed by the processor's trailing pack phase.  The
body is 277 B hot; the election grew the inline fn 64,715 -> 65,047 B,
leaving 489 B under the 64 KiB line.  The election rests on both
views: `CONST` is 13.6% of churn dispatches with a 16% CONST->CONST
2-gram, and the same-day emission shows 161 CONST2 rows against 1,218
surviving bare CONST -- about a fifth of former CONST rows packed.
`TAIL_CALL` (164 B hot, zero emission and zero execution) remains the
first donor whenever budget is next needed.

**The trailing pack phase (compiler-side).**  All bulk-load packing
(CONST2, VAR2/VAR4, FAST2/FAST4) moved to a single pack_runs sweep
that runs after convert_fast, so FAST runs now pack directly instead
of through the VAR2->FAST2 lowering, and mixed-eligibility pairs
lower per-name rather than freezing as unlowered VAR2.  Emission
shows FAST2 x274 and FAST4 x10 under the new placement.  No handler
moved.

**All-constant comparison folding (compiler-side).**  A squish rule
folds all-CONST OBJECT_COMPARE chains to a CONST bool at compile
time, feeding the existing jump elisions.  Emission-only; the
both-const CHECK_COMPARE_VAR case the fusion has always declined is
now genuinely claimed upstream.

## Execution profile (churn, `KOS_TRACE_TIMING`)

From `design/trace/trace-20260820-1710.json`: 52.76e9 dispatches over
the fourteen churn kernels, the first trace of the 215-opcode build
with CONST2 and the trailing pack placement live.  The timing build wraps
every dispatch in two `clock_gettime` calls, so absolute nanoseconds
are inflated and the per-opcode averages compress towards the wrapper
cost.  Counts and shares are the usable signal; avg_ns only separates
the genuinely expensive handlers from the rest.

| opcode | dispatches | % dispatches | % time | body B | temp |
|---|---:|---:|---:|---:|---|
| `OBJECT_OPERAND` | 12.31e9 | 23.34 | 27.19 | 62 | hot |
| `FAST` | 10.33e9 | 19.57 | 15.13 | 221 | hot |
| `CONST` | 7.17e9 | 13.58 | 10.28 | 153 | hot |
| `FAST_STORE` | 3.31e9 | 6.27 | 5.57 | 314 | hot |
| `FAST2` | 2.44e9 | 4.61 | 3.61 | 573 | hot |
| `FAST_ATTR` | 2.40e9 | 4.55 | 4.34 | 541 | hot |
| `FAST_ATTR_INPLACE` | 2.40e9 | 4.55 | 6.68 | 663 | hot |
| `FAST_INPLACE` | 2.34e9 | 4.43 | 5.95 | 78 | hot |
| `CONTINUE` | 2.20e9 | 4.17 | 3.67 | 364 | hot |
| `CHECK_COMPARE` | 1.11e9 | 2.10 | 1.96 | 652 | hot |
| `ITER_NEXT` | 0.91e9 | 1.73 | 2.06 | 615 | hot |
| `CHECK_COMPARE_FAST` | 0.90e9 | 1.70 | 1.61 | 902 | hot |

The sizing consequence is the coverage number, not the ranking.  Only
73 of the 215 opcodes execute at all on churn.  Of the 127 hot
handlers, 55 fire (19,442 B) and 72 never fire (19,756 B) -- half of
the inline budget is code this workload never touches.  Eighteen cold
handlers fire, six of them enough to notice: `OBJECT_INPLACE` 227.6M,
`MATCH_CASE` 50.7M, `MATCH_STORE_NAMES` 10.7M, `UNPACK_FAST` 3.87M,
`YIELD_FROM` 1.31M, `TAIL_CALL_ATTR` 145K dispatches.  `OBJECT_INPLACE` is
traffic from the kernels' in-place subscript shape; at 62 B it is the
cold mirror of `OBJECT_OPERAND`, so promoting it hot is near-free
inline budget if the library traces agree it is common.  Neither list
is otherwise an action item on its own, because churn is
numeric-kernel code; the emission view below is what says whether an
unfired handler is rare everywhere or just rare here.

Two supporting facts from the same trace.  The opcode caches are
effectively saturated: 5.10e9 field lookups for 56 misses and 22,022
generic fallbacks, 300.2M method lookups for 145K misses -- identical
to the digit across three traces now.  And the post-handler
dispatch layer costs 121.0e9 ns over 1.98e9 invocations, 9.2% of
instrumented eval time, 55% of that in `return_boundary` -- which is
where the plain-return fast arm below was aimed.

The count lineage runs unbroken through `trace-20260805-1822.json`,
`trace-20260819-0642.json`, and `trace-20260819-2329.json`: the
kernel set is unchanged and, promotion relabels aside, no per-opcode
count has moved by more than a handful of harness-side dispatches.
Treat this trace as the count baseline for future deltas.  The
earlier caveat stands: parameterize, `with` and delimited-continuation
code paths remain invisible to these kernels.

**CONST2 and the pack placement under execution.**  Against
`trace-20260819-2329.json` the delta is surgical: the total moved by
exactly -16,472 dispatches, and `CONST2` fired exactly 16,472 times
at 21 avg ns (under the 24 ns dispatch mean) -- every delta dispatch
is a collapsed CONST pair, and nothing else moved by more than
harness noise.  The kernels' constant traffic is loop-carried single
loads, not adjacent pairs, so CONST2's churn share is trivial
(3.1e-7); its real population is the library- and tool-shaped code
the emission view counts at 161 rows.  The trailing pack placement is
execution-invariant here: `FAST2` (2.435e9) and `FAST4` (14.79M)
counts are unchanged from packing before versus after the lowering,
confirming the refactor relocated the pass without changing its
product on function-mode streams.  `VAR2` fires twice (module-level
scaffolding), `VAR4` not at all.

**The twins under execution.**  Against the pre-promotion trace
(`trace-20260819-0642.json`) the relabels are exact and everything
else is untouched: `CALL_ATTR` 145,200 -> `TAIL_CALL_ATTR` 145,200
(the attr-tier reversal live, cold to cold) and `TAIL_CALL` 38,866 ->
`TAIL_CALL_FAST` 38,866 (hot to hot); no other per-opcode count moved
by more than harness noise (-47K of 52.76e9 total), the opcache
counters are identical to the digit, and the post-handler layer is
unchanged.  The kernels' heavy call traffic -- `CALL_FAST` 507.4M and
`CALL_FAST_ATTR` 300.2M -- is all non-tail and did not move.
Notably, `TAIL_CALL_FAST_ATTR` fired **zero** times: its hot election
still rests on emission evidence alone (26 corpus sites), and plain
`TAIL_CALL` is now zero in both execution and emission, firming it up
as the first donor when inline budget is next needed.

## Measuring a change

Use `perf stat -e instructions:u` over the churn kernels, not wall
clock.  Wall clock on this workload cannot separate a change from code
layout: at the fast arm, `field` ran 0.63% fewer instructions and 0.9%
slower, while `tree` gained 3.0% wall clock for a 0.45% instruction
drop.  Repeat runs of an identical binary varied by up to 2.5%.

Instruction counts are deterministic, so one run per build suffices.
`perf` adds no measurable overhead, unlike the `KOS_TRACE_TIMING` build,
which wraps every dispatch in two `clock_gettime` calls and inflates a
churn run past 45 minutes.  Subtract a startup floor: a `--samples 0
--warmup 0` invocation fails at report time but only after the full
bootstrap, so its count is the floor to remove from a `--samples 1
--warmup 1` run of two batches.

The plain-return fast arm (17cd085c) measured, per batch, startup
subtracted: `binary_trees` -3.45%, `field` -0.63%, `tree` -0.45%,
`method` +0.08%, `loop` +0.13%, `nbody` +0.30%.  Aggregate over the six
-0.106%; the bootstrap, 157e9 instructions of call-heavy Koskari,
-0.155%.  The win concentrates in return-dense code.  Forcing
`eval_ng_plain_return` back inline was measured and rejected: it costs
8,886 B, because the body owns frame teardown and drags
`kos_dynframe_free`, `KosStack_Release` and `ns_dealloc` in with
it.  Splitting into an always-inlined core for the fast arm plus an
out-of-line wrapper for the general path measured byte for byte
identical, so the cost is intrinsic to the fast arm's copy and there is
no targeted variant to retreat to.  Do not re-try this.

The wall-clock figures originally recorded for the arithmetic handler
promotions (da789034: `field` -3.3%, `loop` -2.3%, `nbody` -1.9%) came
from 3-sample timing and predate this method.  They are below the noise
floor measured above and should be treated as unverified until re-run
under `perf`.

## Remaining levers

- **PGO** ([interpreter/pgo-build.md](interpreter/pgo-build.md)) for
  layout of the inline body.
- **Opcode-design consolidation** (string interpolation, operand
  fusion, item access) rather than further carving -- see the emission
  view below for the targets.
- **A library-shaped trace.**  Churn reaches a third of the opcode set.
  Until scheduler/mcp/repl/dis are traced, the hot/cold split for the
  other two thirds rests on the emission view and on reasoning.

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Emission n-grams (opgrams, same-day corpus)

The sizing view above says where inline bytes live; the emission view
says which *sequences* the compiler produces -- the fusion targets.
Regenerate the two together (`tox -qe opgrams`) so both are read
against the same tree.

The corpus moves to 283 codes; blocks 2,062 -> 2,074.  The headline
change is CONST2 appearing in emission: 161 CONST2 rows against 1,218
surviving bare `CONST` -- 322 former CONST rows packed, about a fifth
of the population, matching the 16% CONST->CONST 2-gram that motivated
the opcode plus the fold-and-pack interactions.  The successor mix
says the runs skew long: `CONST2 -> CONST2` 17% (runs of four or
more) and `CONST2 -> CONST` 17% (odd runs of three), with
`CALL_FAST_ATTR_NAMES` 23% and `CALL_FAST_ATTR` 11% as the leading
consumers -- constant argument blocks feeding fused calls.  The
remaining bare CONST population's top successors are `JUMP` 18% and
`FAST` 18%, pairs the packer correctly cannot touch.  Under the
trailing pack placement FAST runs pack directly: `FAST2` x274,
`FAST4` x10.

Top weighted 2-grams (loop-depth weighted, base 100), otherwise stable
against the previous sweep:

| weighted | raw | pattern | reading |
|---:|---:|---|---|
| 2.11M | 100 | `CONST FAST_FORMAT` | literal segment, then a fused interpolation |
| 2.09M | 95 | `FAST_FORMAT CONST` | fused interpolation, then the next literal segment |
| 2.05M | 19 | `PUSH_LOOP CHECK_COMPARE_FAST` | the fused loop head |
| 2.00M | 3 | `FAST_STORE FAST4` | store then bulk reload |
| 2.00M | 2 | `FAST4 ITEM` (and `FAST2 ITEM`, 1.03M) | indexed access off bulk loads; a FAST-keyed ITEM fusion candidate |
| 2.00M | 2 | `ITEM ITEM_STORE` | element move between subscripts |
| 1.14M | 57 | `OBJECT_OPERAND FAST_STORE` | compute-then-store; the fused store-destination quad target |
| 1.14M | 101 | `PUSH_FOREACH ITER_NEXT` | the foreach head, a fixed shape |
| 1.06M | 55 | `FAST BUILD_ARGS` | load a local into call-argument assembly |
| 1.05M | 39 | `FAST_FORMAT OBJECT_STR` | fused interpolation then stringify |
| 1.04M | 20 | `FAST_INPLACE_CONST CONTINUE` | the loop tail: constant increment in one dispatch |
| 1.04M | 30 | `ITEM FAST_STORE` | subscript into a local |
| 1.03M | 27 | `FAST2 ITEM` | indexed access off a paired load |
| 1.02M | 42 | `FAST2 FAST` | bulk load feeding a single load |

**The interpolation chain remains the top target, unchanged by this
sweep.**  The alternation of literal segments and formatted values is
still the densest sequence in the corpus: at 3 the periodic
`FAST_FORMAT CONST FAST_FORMAT` (2.06M / 36) and `CONST FAST_FORMAT
CONST` (1.06M / 59).  The successor table is as unambiguous as before:
`FAST_FORMAT -> CONST` 68%, `-> OBJECT_STR` 28%, `-> FAST_FORMAT` 4% --
every `FAST_FORMAT` exit is chain traffic.  The residual
`OBJECT_FORMAT` (computed objects only) splits `-> CONST` 83% / `->
OBJECT_STR` 17%.  A format-with-literal-tail opcode, or an n-ary join
that consumes the whole chain in one dispatch, is still the collapse
that remains; the fusion removed the loads, not the alternation.

Other successor facts held steady: `PUSH_FOREACH -> ITER_NEXT` 100%
and `PUSH_LOOP -> CHECK_COMPARE_FAST` 58% (loop heads are fixed
shapes); `FAST_INPLACE_CONST -> CONTINUE` 57% (the loop tail);
`BUILD_LIST -> FAST_STORE` 74% and `CALL_FAST -> FAST_STORE` 33%
(produce-then-bind).  `CLEAR_TRY -> JUMP` 93% is a try-block exit shape.

Operand feeders held steady too: `CONST_ITEM`, `FAST2`, `FAST` and
`CONST` reach `OBJECT_OPERAND` 14%, 13%, 7% and 6% of the time.
Operand fusion pays in the arithmetic kernels and barely at all in the
library and tool code -- which is exactly what the churn trace above
shows from the other side, `OBJECT_OPERAND` at 23% of all dispatches
there.

Read together with the sizing baseline, the three views agree: the
remaining wins are opcode-design consolidation -- the interpolation
chain join first, then the operand-fusion family and item access --
plus PGO for layout.

## Full per-handler sizes (bytes, descending)

Temperature is the handler macro: `hot` = `KOS_OP_HANDLER`
(always-inlined into the dispatch fn), `cold` = `KOS_OP_HANDLER_COLD`
(out of line; the body appears here but costs the inline fn nothing).

| bytes | handler | temp |
|---:|---|---|
| 5,362 | `YIELD_FROM` | cold |
| 1,712 | `CALL_ATTR_STAR` | cold |
| 1,626 | `TAIL_CALL_ATTR_STAR` | cold |
| 1,600 | `IMPORT_USE` | cold |
| 1,431 | `TAIL_CALL_FAST_ATTR_STAR` | cold |
| 1,431 | `CALL_FAST_ATTR_STAR` | cold |
| 1,417 | `PUSH_WIND` | cold |
| 1,364 | `TAIL_CALL_VAR_ATTR_STAR` | cold |
| 1,348 | `CALL_VAR_ATTR_STAR` | cold |
| 1,254 | `TAIL_CALL_FAST_ATTR_NAMES` | cold |
| 1,254 | `CALL_FAST_ATTR_NAMES` | cold |
| 1,210 | `TAIL_CALL_STAR` | cold |
| 1,210 | `CALL_STAR` | cold |
| 1,164 | `TAIL_CALL_VAR_ATTR_NAMES` | cold |
| 1,164 | `CALL_VAR_ATTR_NAMES` | cold |
| 1,125 | `BUILD_CLASS` | cold |
| 1,087 | `MATCH_CASE` | cold |
| 1,061 | `IMPORT_STAR` | cold |
| 1,057 | `PAIR_SPLICE` | cold |
| 1,034 | `TAIL_CALL_FAST_ATTR` | hot |
| 1,027 | `COMBINE_ARGS` | cold |
| 1,018 | `CALL_FAST_ATTR` | hot |
| 1,014 | `TAIL_CALL_FAST_STAR` | cold |
| 1,014 | `CALL_FAST_STAR` | cold |
| 955 | `TAIL_CALL_VAR_STAR` | cold |
| 938 | `TAIL_CALL_VAR_ATTR` | cold |
| 928 | `CALL_VAR_STAR` | cold |
| 922 | `CALL_VAR_ATTR` | cold |
| 902 | `CHECK_COMPARE_FAST` | hot |
| 876 | `BUILD_FUNCTION` | cold |
| 864 | `PUSH_WITH` | cold |
| 858 | `PUSH_NS` | hot |
| 845 | `UNPACK_FAST` | cold |
| 832 | `PUSH_DYN_WITH` | cold |
| 824 | `UNPACK_STAR` | cold |
| 809 | `FAST_ATTR_STORE` | hot |
| 766 | `COMBINE_KWDS` | cold |
| 717 | `RAISE` | cold |
| 674 | `CHECK_COMPARE_VAR` | cold |
| 663 | `FAST_ATTR_INPLACE` | hot |
| 652 | `CHECK_COMPARE` | hot |
| 630 | `POP_SHIFT` | cold |
| 615 | `ITER_NEXT` | hot |
| 595 | `OBJECT_FORMAT` | hot |
| 587 | `TAIL_CALL_ATTR_NAMES` | cold |
| 587 | `CALL_ATTR_NAMES` | cold |
| 577 | `POP_PROMPT` | cold |
| 573 | `FAST2` | hot |
| 571 | `FAST_INPLACE_CONST` | hot |
| 557 | `POP_FINALLY` | cold |
| 550 | `VAR_ATTR_STORE` | cold |
| 546 | `EXCEPTION_MATCH` | cold |
| 543 | `SWITCH_EQ_FAST_ATTR` | hot |
| 541 | `ITEM_TAKE` | cold |
| 541 | `FAST_ATTR` | hot |
| 530 | `VAR_ATTR_INPLACE` | cold |
| 529 | `IMPORT_FROM` | cold |
| 528 | `PROPER` | hot |
| 517 | `TAIL_CALL_FAST_NAMES` | cold |
| 517 | `CALL_FAST_NAMES` | cold |
| 506 | `UNPACK_VAR` | cold |
| 505 | `CDR` | hot |
| 498 | `FAST_LIST_APPEND` | hot |
| 495 | `CALL_NAMES` | cold |
| 494 | `CAR` | hot |
| 490 | `PUSH_FOREACH` | hot |
| 483 | `UNPACK` | cold |
| 474 | `FAST_ATTR_TAKE` | hot |
| 460 | `SWITCH_EQ_VAR_ATTR` | cold |
| 457 | `OBJECT_DIVMOD` | cold |
| 449 | `POP_DYN_WITH` | hot |
| 445 | `VAR_LIST_APPEND` | cold |
| 445 | `ITEM` | hot |
| 445 | `ITEM_REF` | hot |
| 445 | `BUILD_LIST_EXTEND_CONST` | hot |
| 439 | `SWITCH_EQ_FAST` | hot |
| 437 | `TAIL_CALL_VAR_NAMES` | cold |
| 437 | `CALL_VAR_NAMES` | cold |
| 429 | `CHECK_EXCEPTION` | hot |
| 426 | `PUSH_SHIFT` | hot |
| 425 | `PARAM` | hot |
| 423 | `FAST4` | hot |
| 420 | `BUILD_LIST_EXTEND` | hot |
| 417 | `ATTR_INPLACE` | cold |
| 411 | `VAR_ATTR` | cold |
| 410 | `OBJECT_COMPARE` | hot |
| 410 | `ATTR_TAKE` | hot |
| 399 | `SWITCH_EQ` | hot |
| 397 | `OBJECT_STR` | hot |
| 397 | `CONST_ITEM_TAKE` | hot |
| 395 | `EXCEPTION` | hot |
| 394 | `SWITCH_EQ_VAR` | cold |
| 384 | `CONS` | hot |
| 381 | `TAIL_CALL_FAST` | hot |
| 381 | `CALL_FAST` | hot |
| 378 | `VAR_ATTR_TAKE` | cold |
| 378 | `FAST_ATTR_REF` | hot |
| 374 | `OBJECT_NEXT` | cold |
| 370 | `BREAK` | hot |
| 365 | `MATCH_STORE_NAMES` | cold |
| 365 | `CALL_ATTR` | cold |
| 364 | `CONTINUE` | hot |
| 362 | `VAR2` | cold |
| 362 | `TAIL_CALL_ATTR` | cold |
| 362 | `OBJECT_COMPARE_FAST` | hot |
| 362 | `CONST_ITEM_STORE` | hot |
| 354 | `FAST_NEXT` | cold |
| 351 | `VAR4` | cold |
| 349 | `ATTR_STORE` | hot |
| 333 | `VAR_BIND` | cold |
| 330 | `BUILD_LIST_APPEND` | hot |
| 322 | `FAST_FORMAT_CONST` | hot |
| 314 | `JUMP_OR` | hot |
| 314 | `JUMP_AND` | hot |
| 314 | `ITEM_CLEAR` | hot |
| 314 | `FAST_STORE` | hot |
| 311 | `VAR_TAKE` | cold |
| 306 | `VAR_INPLACE_CONST` | cold |
| 304 | `POP_WITH` | hot |
| 303 | `TAIL_CALL_VAR` | cold |
| 303 | `CALL_VAR` | cold |
| 301 | `TAIL_CALL_NAMES` | cold |
| 298 | `ITEM_STORE` | hot |
| 293 | `BUILD_LIST` | hot |
| 292 | `VAR_NEXT` | cold |
| 289 | `CONST_ITEM` | hot |
| 287 | `OBJECT_COMPARE_VAR` | cold |
| 287 | `NILP` | hot |
| 285 | `OBJECT_VARS` | hot |
| 282 | `OBJECT_TRUTH` | hot |
| 282 | `OBJECT_NOT` | hot |
| 282 | `FAST_ITER` | hot |
| 282 | `FAST_FORMAT` | hot |
| 279 | `SWITCH_EQ_CONST` | hot |
| 277 | `CONST_ITEM_REF` | hot |
| 277 | `CONST2` | hot |
| 277 | `BUILD_KWDS_NAMES` | hot |
| 275 | `OBJECT_FORMAT_CONST` | hot |
| 275 | `ATTR` | hot |
| 275 | `ATTR_REF` | hot |
| 273 | `POP_WIND` | hot |
| 259 | `VAR_ATTR_REF` | cold |
| 258 | `OBJECT_LEN` | hot |
| 253 | `OBJECT_DIR` | hot |
| 250 | `VAR_STORE` | cold |
| 250 | `FAST_ATTR_CLEAR` | hot |
| 247 | `FAST_BIND` | hot |
| 245 | `VAR_FORMAT_CONST` | cold |
| 239 | `OBJECT_TYPE` | hot |
| 237 | `OBJECT_REPR` | hot |
| 237 | `OBJECT_ITER` | hot |
| 237 | `OBJECT_BIT_INVERT` | hot |
| 237 | `DEREF` | hot |
| 236 | `POP_FOREACH` | hot |
| 234 | `VAR_REF` | cold |
| 231 | `GLOBALS` | hot |
| 228 | `POP_TRY` | hot |
| 221 | `VAR_ITER` | hot |
| 221 | `VAR_FORMAT` | cold |
| 221 | `FAST` | hot |
| 220 | `PARAM_CONST` | hot |
| 218 | `IMPORT` | hot |
| 218 | `BUILD_KWDS` | hot |
| 215 | `PUSH_LOOP` | hot |
| 205 | `JUMP_TRUE` | hot |
| 205 | `JUMP_FALSE` | hot |
| 204 | `POP_LOOP` | hot |
| 202 | `POP_COLLECT` | hot |
| 202 | `BUILD_ARGS` | hot |
| 197 | `PUSH_PROMPT` | hot |
| 191 | `FAST_STORE_CONST` | hot |
| 191 | `CAPTURE_CC` | hot |
| 189 | `AWAIT` | hot |
| 183 | `RETURN` | hot |
| 182 | `CHECK_NIL` | hot |
| 180 | `CONST_ITEM_CLEAR` | hot |
| 178 | `PUSH_COLLECT` | hot |
| 178 | `ATTR_CLEAR` | hot |
| 173 | `PUSH_CC` | hot |
| 173 | `BUILD_LIST_APPEND_CONST` | hot |
| 169 | `THEEND` | cold |
| 167 | `YIELD` | hot |
| 164 | `TAIL_CALL` | hot |
| 164 | `CALL` | hot |
| 162 | `VAR_ATTR_CLEAR` | cold |
| 159 | `VAR` | cold |
| 159 | `BUILD_LIST_CONST` | hot |
| 154 | `FAST_REF` | hot |
| 153 | `CONST` | hot |
| 151 | `VAR_CLEAR` | cold |
| 146 | `PUSH_PARAM` | hot |
| 141 | `POP` | hot |
| 138 | `FAST_TAKE` | hot |
| 124 | `VAR_INPLACE` | cold |
| 103 | `VAR_STORE_CONST` | cold |
| 103 | `LOCALS` | hot |
| 101 | `CLEAR_LOOP` | hot |
| 101 | `CLEAR_FOREACH` | hot |
| 88 | `CLEAR_TRY` | hot |
| 88 | `CLEAR_FINALLY` | hot |
| 78 | `FAST_INPLACE` | hot |
| 76 | `PUSH_TRY` | hot |
| 76 | `PUSH_FINALLY` | hot |
| 62 | `OBJECT_OPERAND` | hot |
| 62 | `OBJECT_INPLACE` | cold |
| 56 | `FAST_CLEAR` | hot |
| 56 | `DUP` | hot |
| 52 | `POP_PARAM` | hot |
| 52 | `POP_NS` | hot |
| 52 | `POP_CC` | hot |
| 52 | `EXCEPTION_CLEAR` | hot |
| 35 | `JUMP` | hot |
| 28 | `POSITION` | cold |
| 28 | `NOOP` | cold |
| 28 | `LABEL` | cold |

<!-- Regenerate: measurement build with `KOS_ALL_COLD=1 uv pip install -qe .`
     (the hints.h gate; passenv'd through setup.py's env-to-define list), then
     `nm --print-size` on the .so, grep ' t op_[A-Z]'. Fold `.cold` and
     `.lto_priv.N` suffixes back into the base symbol, and drop the
     `op_add`-style operator cores from operator.c, which share the prefix but
     are not handlers. Dispatch fn size and the noinline floor are the
     `koskari_eval_dispatch_computed_goto` symbol in the normal and
     KOS_ALL_COLD builds respectively. The Makefile rebuild guard is a content
     hash over koskari/lib and does not see the env change, so
     `make clean-built && rm -f .venv/.koskari-srchash` between builds.
     Rebuild without the env afterward. Table is two columns, one opcode per
     row (diff-friendly), descending by size with reverse-alphabetical ties.
     Regenerate the emission view alongside: `tox -qe opgrams` (add `--json`
     for exact successor percentages). The execution profile comes from the
     newest `design/trace/trace-*.json`, produced by `tox -qe churn-trace`.
     See Methodology. -->
