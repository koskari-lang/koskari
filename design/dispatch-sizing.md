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
the fully-inlined dispatch fn (61,365 B) is smaller than floor + bodies
(104,759 B), so at `-O2 -flto` inlining shares tails and specialises away more
than the standalone bodies imply.  Shared static cores called from several
handlers sit outside the `op_` symbols and are not in the table (e.g.
`object_next_step`, the 1,367 B step core behind the three cold NEXT forms --
which is why OBJECT_NEXT reads far smaller than the old monolithic NEXT).
The operator cores in `operator.c` (`op_add`, `op_bit_and`, ...) share the
`op_` prefix but are not handlers; they are excluded from every figure here.
67 handlers are `KOS_OP_HANDLER_COLD` -- their bodies appear in the table below
but do not contribute to the inlined function at all.

Hotness is now backed by measurement for one workload: the
`KOS_TRACE_TIMING` build records per-opcode counts, and the churn
kernels are traced into `trace/`.  That covers the numeric-kernel
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

Measured 2026-07-28 on `master` at d05cbfa4, 192 opcodes (one handler
each; 193 symbols counting the split-out `GLOBALS.cold`).

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 61,365 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 192 handlers un-inlined) | 24,082 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 192 + loop scaffold |
| handler bodies | 80,677 | sum of standalone noinline bodies -- 42,546 B across the 67 cold handlers, 38,131 B across the 125 hot ones (the actual inline budget) |

Distribution (top-heavy, long-tailed; % of the 80,677 B body):

- `>= 1000 B`: 12 handlers, 18,948 B (23% of body)
- `>=  500 B`: 48 handlers, 42,632 B (53% of body)
- `>=  200 B`: 147 handlers, 75,313 B (93% of body)
- `>=  100 B`: 175 handlers, 79,708 B (99% of body)

The floor grew 23,954 -> 24,082 B against the previous sweep: +128 B is
one opcode's worth of glue, matching the one added opcode.

## Applied changes since the previous sweep

**Parameter resolution (37a9d5ee, PR #108).** The parameterize
redesign put parameter resolution on a shadow table and root scope, and
added `PARAM_CONST` as the constant-key form.  `PARAM` went 162 -> 464 B
and `PARAM_CONST` is 220 B, both `KOS_OP_HANDLER`; the split moved
124/67 -> 125/67.  Their combined +522 B accounts for the whole +528 B
the dispatch fn grew (60,837 -> 61,365), so nothing else in the tree
moved the inline budget.

**Delimited-continuation and `with` rework (PR #111; plus 00e860cf,
koskari-defined context managers run as installed frames).**  Net near
zero on the inline fn: `PUSH_PROMPT` +78 and `POP_WITH` +43 against
`PUSH_SHIFT` -48, so
+73 B across the hot members.  The larger swings landed in cold
handlers, where they cost the dispatch fn nothing: `POP_SHIFT` -82,
`POP_PROMPT` +42, `PUSH_WITH` +16.  Every other delta in the table is
alignment padding (+/-16 B or less).

## Execution profile (churn, `KOS_TRACE_TIMING`)

From `trace/trace-20260728-1044.json`: 52.19e9 dispatches over the
thirteen churn kernels.  The timing build wraps every dispatch in two
`clock_gettime` calls, so absolute nanoseconds are inflated and the
per-opcode averages compress towards the wrapper cost.  Counts and
shares are the usable signal; avg_ns only separates the genuinely
expensive handlers from the rest.

| opcode | dispatches | % dispatches | % time | body B | temp |
|---|---:|---:|---:|---:|---|
| `OBJECT_OPERAND` | 12.50e9 | 23.95 | 28.03 | 62 | hot |
| `FAST` | 10.05e9 | 19.25 | 14.84 | 221 | hot |
| `CONST` | 7.15e9 | 13.70 | 10.35 | 154 | hot |
| `FAST_STORE` | 3.26e9 | 6.25 | 5.52 | 301 | hot |
| `FAST2` | 2.62e9 | 5.02 | 3.92 | 569 | hot |
| `FAST_ATTR` | 2.40e9 | 4.60 | 4.29 | 541 | hot |
| `FAST_ATTR_INPLACE` | 2.40e9 | 4.60 | 6.77 | 663 | hot |
| `FAST_INPLACE` | 2.34e9 | 4.48 | 6.04 | 78 | hot |
| `CONTINUE` | 2.19e9 | 4.20 | 3.67 | 370 | hot |
| `CHECK_COMPARE` | 1.11e9 | 2.13 | 1.96 | 636 | hot |
| `ITER_NEXT` | 0.90e9 | 1.73 | 1.99 | 647 | hot |
| `CHECK_COMPARE_FAST` | 0.89e9 | 1.71 | 1.59 | 918 | hot |

The sizing consequence is the coverage number, not the ranking.  Only
63 of the 192 opcodes execute at all on churn.  Of the 125 hot
handlers, 51 fire (17,866 B) and 74 never fire (20,265 B) -- 53% of the
inline budget is code this workload never touches.  Twelve cold
handlers do fire, three of them enough to notice: `UNPACK_FAST` 3.87M,
`YIELD_FROM` 1.31M, `CALL_ATTR` 145K dispatches.  Neither list is an
action item on its own, because churn is numeric-kernel code; the
emission view below is what says whether an unfired handler is rare
everywhere or just rare here.

Two supporting facts from the same trace.  The opcode caches are
effectively saturated: 5.10e9 field lookups for 49 misses and 21,893
generic fallbacks, 300.2M method lookups for 145K misses.  And the
post-handler dispatch layer costs 122.6e9 ns over 1.98e9 invocations,
9.4% of instrumented eval time, 55% of that in `return_boundary` --
which is where the plain-return fast arm below was aimed.

Comparing against `trace/trace-20260725-0937.json`, the per-opcode
counts are identical to within 4 dispatches out of 52e9.  The
parameterize, `with`
and delimited-continuation work landed in between and churn cannot see
any of it; do not use these kernels to measure changes in that
territory.

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
`kos_dynframe_free`, `KosStackBlock_Release` and `ns_dealloc` in with
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

The corpus roughly doubled since the previous sweep, 105 codes / 783
blocks -> 201 codes / 1,754 blocks, because the developer tools are now
written in Koskari (`tools.tools`, `tools.opgrams`,
`tools.editor_tokens.regen`, and the corpus loader, exporter, oracle
and views).  That is string, dict, path and argparse work, and it moves
the weighted ranking substantially: the numeric kernels no longer own
the top of the list.

Top weighted 2-grams (loop-depth weighted, base 100):

| weighted | raw | pattern | reading |
|---:|---:|---|---|
| 3.14M | 124 | `FAST OBJECT_FORMAT` | interpolate a local into a template |
| 2.18M | 230 | `CONST FAST` | the generic feeder pair |
| 2.11M | 119 | `OBJECT_FORMAT CONST` | format, then the next literal segment |
| 2.05M | 19 | `PUSH_LOOP CHECK_COMPARE_FAST` | the fused loop head |
| 2.01M | 3 | `FAST4 ITEM` (and `FAST2 ITEM`, 1.03M) | indexed access off bulk loads; a FAST-keyed ITEM fusion candidate |
| 2.00M | 2 | `ITEM ITEM_STORE` | element move between subscripts |
| 1.17M | 244 | `FAST_STORE FAST` | store then reload |
| 1.14M | 51 | `OBJECT_OPERAND FAST_STORE` | compute-then-store; the fused store-destination quad target |
| 1.14M | 82 | `PUSH_FOREACH ITER_NEXT` | the foreach head, a fixed shape |
| 1.05M | 40 | `OBJECT_FORMAT OBJECT_STR` | format then stringify |
| 1.04M | 20 | `FAST_INPLACE_CONST CONTINUE` | the loop tail: constant increment in one dispatch |
| 1.02M | 75 | `CONST OBJECT_OPERAND` | operand feeder traffic still unfused |

**String interpolation is the new top target.**  An f-string emits an
alternating chain of literal segments and formatted values, and it
shows up at every n: `CONST FAST OBJECT_FORMAT` (2.11M / 86 raw), `FAST
OBJECT_FORMAT CONST` (2.09M / 85), and at 4 the periodic
`OBJECT_FORMAT CONST FAST OBJECT_FORMAT` (2.08M / 40).  The successor
table is unambiguous: `OBJECT_FORMAT -> CONST` 73% and `OBJECT_FORMAT
-> OBJECT_STR` 24%, 97% of all `OBJECT_FORMAT` exits between them.  A
format-with-literal-tail opcode, or an n-ary join that consumes the
whole chain in one dispatch, would collapse the densest sequence in the
corpus.  `OBJECT_FORMAT` is a 595 B hot handler already, so this is
consolidation, not new inline budget.

Other successor facts worth acting on: `PUSH_FOREACH -> ITER_NEXT` 100%
and `PUSH_LOOP -> CHECK_COMPARE_FAST` 58% (loop heads are fixed
shapes); `FAST_INPLACE_CONST -> CONTINUE` 57% (the loop tail);
`BUILD_LIST -> FAST_STORE` 76% and `CALL_FAST -> FAST_STORE` 30%
(produce-then-bind).  `CLEAR_TRY -> JUMP` 88% is a try-block exit shape.

Operand feeders diluted further as the corpus widened: `CONST_ITEM`,
`FAST2`, `FAST` and `CONST` reach `OBJECT_OPERAND` 14%, 13%, 7% and 7%
of the time, against 18%, 20% and 11% in the previous sweep.  Operand
fusion pays in the arithmetic kernels and barely at all in the library
and tool code -- which is exactly what the churn trace above shows from
the other side, `OBJECT_OPERAND` at 24% of all dispatches there.

Read together with the sizing baseline, the three views agree: the
remaining wins are opcode-design consolidation -- string interpolation
first, then the operand-fusion family and item access -- plus PGO for
layout.

## Full per-handler sizes (bytes, descending)

Temperature is the handler macro: `hot` = `KOS_OP_HANDLER`
(always-inlined into the dispatch fn), `cold` = `KOS_OP_HANDLER_COLD`
(out of line; the body appears here but costs the inline fn nothing).

| bytes | handler | temp |
|---:|---|---|
| 5,346 | `YIELD_FROM` | cold |
| 1,712 | `CALL_ATTR_STAR` | cold |
| 1,493 | `CALL_VAR_ATTR_STAR` | cold |
| 1,417 | `PUSH_WIND` | cold |
| 1,301 | `CALL_FAST_ATTR_STAR` | cold |
| 1,210 | `TAIL_CALL_STAR` | cold |
| 1,210 | `CALL_STAR` | cold |
| 1,106 | `BUILD_CLASS` | cold |
| 1,061 | `IMPORT_STAR` | cold |
| 1,043 | `COMBINE_ARGS` | cold |
| 1,035 | `PAIR_SPLICE` | cold |
| 1,014 | `CALL_FAST_STAR` | cold |
| 928 | `CALL_VAR_STAR` | cold |
| 918 | `CHECK_COMPARE_FAST` | hot |
| 887 | `BUILD_FUNCTION` | cold |
| 864 | `PUSH_WITH` | cold |
| 842 | `PUSH_NS` | hot |
| 832 | `PUSH_DYN_WITH` | cold |
| 821 | `UNPACK_STAR` | cold |
| 811 | `UNPACK_FAST` | cold |
| 809 | `FAST_ATTR_STORE` | hot |
| 745 | `IMPORT_FROM` | cold |
| 738 | `COMBINE_KWDS` | cold |
| 733 | `RAISE` | cold |
| 680 | `CALL_FAST_ATTR_NAMES` | cold |
| 674 | `CHECK_COMPARE_VAR` | cold |
| 663 | `FAST_ATTR_INPLACE` | hot |
| 647 | `ITER_NEXT` | hot |
| 636 | `CHECK_COMPARE` | hot |
| 630 | `POP_SHIFT` | cold |
| 595 | `OBJECT_FORMAT` | hot |
| 579 | `CALL_VAR_ATTR_NAMES` | cold |
| 577 | `POP_PROMPT` | cold |
| 571 | `FAST_INPLACE_CONST` | hot |
| 569 | `FAST2` | hot |
| 557 | `POP_FINALLY` | cold |
| 550 | `VAR_ATTR_STORE` | cold |
| 546 | `EXCEPTION_MATCH` | hot |
| 543 | `SWITCH_EQ_FAST_ATTR` | hot |
| 541 | `FAST_ATTR` | hot |
| 540 | `UNPACK_VAR` | cold |
| 539 | `PROPER` | hot |
| 538 | `ITEM_TAKE` | hot |
| 530 | `VAR_ATTR_INPLACE` | cold |
| 522 | `CALL_FAST_ATTR` | hot |
| 513 | `CDR` | hot |
| 513 | `CAR` | hot |
| 503 | `CALL_FAST_NAMES` | cold |
| 498 | `FAST_LIST_APPEND` | hot |
| 491 | `CALL_ATTR_NAMES` | cold |
| 490 | `PUSH_FOREACH` | hot |
| 474 | `FAST_ATTR_TAKE` | hot |
| 468 | `UNPACK` | hot |
| 464 | `PARAM` | hot |
| 460 | `SWITCH_EQ_VAR_ATTR` | cold |
| 450 | `OBJECT_DIVMOD` | cold |
| 449 | `POP_DYN_WITH` | hot |
| 447 | `CALL_NAMES` | cold |
| 445 | `VAR_LIST_APPEND` | cold |
| 445 | `ITEM` | hot |
| 445 | `BUILD_LIST_EXTEND_CONST` | hot |
| 442 | `ITEM_REF` | hot |
| 439 | `SWITCH_EQ_FAST` | hot |
| 429 | `CHECK_EXCEPTION` | hot |
| 426 | `PUSH_SHIFT` | hot |
| 426 | `OBJECT_COMPARE` | hot |
| 426 | `CALL_VAR_ATTR` | cold |
| 423 | `FAST4` | hot |
| 420 | `BUILD_LIST_EXTEND` | hot |
| 411 | `VAR_ATTR` | cold |
| 410 | `ATTR_TAKE` | hot |
| 402 | `CONST_ITEM_TAKE` | hot |
| 401 | `ATTR_INPLACE` | cold |
| 399 | `SWITCH_EQ` | hot |
| 399 | `CALL_VAR_NAMES` | cold |
| 395 | `EXCEPTION` | hot |
| 394 | `SWITCH_EQ_VAR` | cold |
| 389 | `CALL_FAST` | hot |
| 387 | `FAST_NEXT` | cold |
| 384 | `CONS` | hot |
| 381 | `OBJECT_STR` | hot |
| 378 | `VAR_ATTR_TAKE` | cold |
| 378 | `FAST_ATTR_REF` | hot |
| 375 | `VAR4` | cold |
| 374 | `OBJECT_NEXT` | cold |
| 371 | `VAR2` | cold |
| 370 | `CONTINUE` | hot |
| 370 | `BREAK` | hot |
| 362 | `OBJECT_COMPARE_FAST` | hot |
| 351 | `CALL_ATTR` | cold |
| 349 | `CONST_ITEM_STORE` | hot |
| 349 | `ATTR_STORE` | hot |
| 333 | `VAR_BIND` | cold |
| 330 | `BUILD_LIST_APPEND` | hot |
| 329 | `ITEM_CLEAR` | hot |
| 314 | `JUMP_OR` | hot |
| 314 | `JUMP_AND` | hot |
| 311 | `VAR_TAKE` | cold |
| 306 | `VAR_INPLACE_CONST` | cold |
| 304 | `POP_WITH` | hot |
| 301 | `TAIL_CALL_NAMES` | cold |
| 301 | `FAST_STORE` | hot |
| 301 | `FAST_ITER` | hot |
| 296 | `ITEM_STORE` | hot |
| 293 | `CALL_VAR` | cold |
| 293 | `BUILD_LIST` | hot |
| 292 | `VAR_NEXT` | cold |
| 287 | `OBJECT_COMPARE_VAR` | cold |
| 287 | `NILP` | hot |
| 285 | `OBJECT_VARS` | hot |
| 279 | `SWITCH_EQ_CONST` | hot |
| 277 | `BUILD_KWDS_NAMES` | hot |
| 275 | `OBJECT_FORMAT_CONST` | hot |
| 275 | `CONST_ITEM_REF` | hot |
| 275 | `CONST_ITEM` | hot |
| 275 | `ATTR_REF` | hot |
| 275 | `ATTR` | hot |
| 274 | `OBJECT_TRUTH` | hot |
| 274 | `OBJECT_NOT` | hot |
| 273 | `POP_WIND` | hot |
| 267 | `OBJECT_LEN` | hot |
| 259 | `VAR_ATTR_REF` | cold |
| 253 | `OBJECT_REPR` | hot |
| 253 | `OBJECT_ITER` | hot |
| 253 | `OBJECT_DIR` | hot |
| 253 | `OBJECT_BIT_INVERT` | hot |
| 250 | `FAST_ATTR_CLEAR` | hot |
| 247 | `FAST_BIND` | hot |
| 245 | `DEREF` | hot |
| 239 | `OBJECT_TYPE` | hot |
| 236 | `POP_FOREACH` | hot |
| 234 | `VAR_REF` | cold |
| 231 | `GLOBALS` | hot |
| 228 | `POP_TRY` | hot |
| 226 | `VAR_STORE` | cold |
| 221 | `FAST` | hot |
| 220 | `PARAM_CONST` | hot |
| 219 | `VAR_ITER` | hot |
| 218 | `BUILD_KWDS` | hot |
| 218 | `BUILD_ARGS` | hot |
| 215 | `PUSH_LOOP` | hot |
| 210 | `IMPORT` | hot |
| 205 | `JUMP_TRUE` | hot |
| 205 | `JUMP_FALSE` | hot |
| 205 | `AWAIT` | hot |
| 204 | `POP_LOOP` | hot |
| 202 | `POP_COLLECT` | hot |
| 197 | `PUSH_PROMPT` | hot |
| 191 | `FAST_STORE_CONST` | hot |
| 191 | `CAPTURE_CC` | hot |
| 183 | `RETURN` | hot |
| 182 | `CHECK_NIL` | hot |
| 178 | `PUSH_COLLECT` | hot |
| 178 | `CONST_ITEM_CLEAR` | hot |
| 178 | `ATTR_CLEAR` | hot |
| 173 | `PUSH_CC` | hot |
| 173 | `BUILD_LIST_APPEND_CONST` | hot |
| 169 | `THEEND` | cold |
| 167 | `YIELD` | hot |
| 164 | `TAIL_CALL` | hot |
| 164 | `CALL` | hot |
| 162 | `VAR_ATTR_CLEAR` | cold |
| 162 | `VAR` | cold |
| 159 | `BUILD_LIST_CONST` | hot |
| 154 | `FAST_REF` | hot |
| 154 | `CONST` | hot |
| 151 | `VAR_CLEAR` | cold |
| 146 | `PUSH_PARAM` | hot |
| 146 | `FAST_TAKE` | hot |
| 141 | `POP` | hot |
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
     newest `trace/trace-*.json`, produced by `tox -qe churn-trace`.
     See Methodology. -->
