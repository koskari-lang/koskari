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
the fully-inlined dispatch fn (60,837 B) is smaller than floor + bodies
(103,595 B), so at `-O2 -flto` inlining shares tails and specialises away more
than the standalone bodies imply.  Shared static cores called from several
handlers sit outside the `op_` symbols and are not in the table (e.g.
`object_next_step`, the 1,351 B step core behind the three cold NEXT forms --
which is why OBJECT_NEXT reads far smaller than the old monolithic NEXT).
The operator cores in `operator.c` (`op_add`, `op_bit_and`, ...) share the
`op_` prefix but are not handlers; they are excluded from every figure here.
67 handlers are `KOS_OP_HANDLER_COLD` -- their bodies appear in the table below
but do not contribute to the inlined function at all.  Hotness is reasoned, not
profiled: there is no PGO/instrumented build yet (deferred until we have
representative profiling apps -- scheduler, koslife, churns, mcp, repl, dis).

Toggling `KOS_ALL_COLD` changes no source, and the Makefile's rebuild
guard is a content hash over `koskari/lib`, so the flag alone will not
trigger a rebuild.  Clear the build first:

    make clean-built && rm -f .venv/.koskari-srchash
    KOS_ALL_COLD=1 make venv

Measured 2026-07-24 on `master` at 17cd085c, 191 opcodes (one handler
each; 192 symbols counting the split-out `GLOBALS.cold`).

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 60,837 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 191 handlers un-inlined) | 23,954 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 191 + loop scaffold |
| handler bodies | 79,641 | sum of standalone noinline bodies -- 42,305 B across the 67 cold handlers, 37,336 B across the 124 hot ones (the actual inline budget) |

Distribution (top-heavy, long-tailed; % of the 79,641 B body):

- `>= 1000 B`: 11 handlers, 17,705 B (22% of body)
- `>=  500 B`: 48 handlers, 42,379 B (53% of body)
- `>=  200 B`: 143 handlers, 73,836 B (93% of body)
- `>=  100 B`: 174 handlers, 78,692 B (99% of body)

The floor grew 23,416 -> 23,954 B against the previous sweep.  That is
loop scaffold, not handler glue: the plain-return fast arm added in
17cd085c lives in `cg_handle_op_result` and so survives `KOS_ALL_COLD`.

## Applied changes since the previous sweep

**Arithmetic handler promotions (da789034).** `OBJECT_OPERAND`,
`FAST_INPLACE`, and `FAST_ATTR_INPLACE` moved from
`KOS_OP_HANDLER_COLD` to `KOS_OP_HANDLER`, taking the split from 121/70
to 124/67 and the dispatch fn from 62,557 to 63,272 B.  They were the
hottest remaining cold handlers on the churn profile: `OBJECT_OPERAND`
alone is 28% of instrumented eval time and 24% of all dispatches, and
the two in-place forms add a further 13%.  `OBJECT_OPERAND` and
`FAST_INPLACE` are small because they are row-decode wrappers over the
still-noinline `kos_object_operand` and `kos_fast_inplace_operand` in
`eval/eval_obj.c`, so promoting them removes one of two out-of-line
calls per execution rather than both.

**Plain-return fast arm (17cd085c).** A fast arm at the head of
`cg_handle_op_result` routes a `KOS_RES_RETURN` whose predicate holds
straight into `eval_ng_plain_return`, skipping the out-of-line
`apply_kos_op_result` call, its switch, and the guard chain.  Dispatch
fn 63,272 -> 60,837 B: giving `eval_ng_plain_return` a second call site
makes it out-of-line, which also drops its inlined copy from
`eval_event_return_at_frame`.

Forcing `eval_ng_plain_return` back inline was measured and rejected.
It costs 8,886 B, because the body owns frame teardown and drags
`kos_dynframe_free`, `KosStackBlock_Release` and `ns_dealloc` in with
it.  Splitting into an always-inlined core for the fast arm plus an
out-of-line wrapper for the general path measured byte for byte
identical, so the cost is intrinsic to the fast arm's copy and there is
no targeted variant to retreat to.  Do not re-try this.

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

The fast arm measured, per batch, startup subtracted: `binary_trees`
-3.45%, `field` -0.63%, `tree` -0.45%, `method` +0.08%, `loop` +0.13%,
`nbody` +0.30%.  Aggregate over the six -0.106%; the bootstrap, 157e9
instructions of call-heavy Koskari, -0.155%.  The win concentrates in
return-dense code.

The wall-clock figures originally recorded for the handler promotions
(`field` -3.3%, `loop` -2.3%, `nbody` -1.9%) came from 3-sample timing
and predate this method.  They are below the noise floor measured
above and should be treated as unverified until re-run under `perf`.

## Remaining levers

- **PGO** ([interpreter/pgo-build.md](interpreter/pgo-build.md)) for
  layout of the inline body.
- **Opcode-design consolidation** (operand fusion, item access) rather
  than further carving -- see the emission view below for the targets.

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Emission n-grams (opgrams, same-day corpus)

The sizing view above says where inline bytes live; the emission view
says which *sequences* the compiler produces -- the fusion targets.
Regenerate the two together (`tox -qe opgrams`, corpus here: 105 codes
/ 783 blocks across bootstrap, cli, life, churn) so both are read
against the same tree.

Top weighted patterns (loop-depth weighted, base 100):

| weighted | raw | pattern | reading |
|---:|---:|---|---|
| 2.05M | 18 | `PUSH_LOOP CHECK_COMPARE_FAST` | the fused loop head |
| 2.01M | 3 | `FAST4 ITEM` (and `FAST2 ITEM`, 1.03M) | indexed access off bulk loads; a FAST-keyed ITEM fusion candidate |
| 2.00M | 2 | `ITEM ITEM_STORE` | element move between subscripts |
| 1.13M | 45 | `OBJECT_OPERAND FAST_STORE` | compute-then-store; the fused store-destination quad target |
| 1.04M | 19 | `FAST_INPLACE_CONST CONTINUE` | the loop tail: constant increment in one dispatch |
| 1.00M | 51 | `FAST CONST OBJECT_OPERAND` | operand feeder traffic still unfused |

Successor facts worth acting on: `PUSH_FOREACH -> ITER_NEXT` 100% and
`PUSH_LOOP -> CHECK_COMPARE_FAST` 62% (loop heads are fixed shapes);
`FAST_INPLACE_CONST -> CONTINUE` 70% (the loop tail);
`OBJECT_FORMAT -> CONST` 79% then `OBJECT_STR -> CALL_FAST_ATTR` 52%
(the interpolate-and-write idiom, string-template territory);
`CALL_FAST_ATTR -> POP` 46% (statement-position method calls).
Operand feeders stay diffuse across the wider corpus -- `FAST2`,
`CONST_ITEM` and `FAST` reach `OBJECT_OPERAND` 20%, 18% and 11% of the
time -- so operand fusion pays most in the arithmetic kernels and least
in the library code.

Read together with the sizing baseline, both views point the same way:
the remaining wins are opcode-design consolidation -- the
operand-fusion family (compute-store, operand feeders) and item
access -- plus PGO for layout.

## Full per-handler sizes (bytes, descending)

Temperature is the handler macro: `hot` = `KOS_OP_HANDLER`
(always-inlined into the dispatch fn), `cold` = `KOS_OP_HANDLER_COLD`
(out of line; the body appears here but costs the inline fn nothing).

| bytes | handler | temp |
|---:|---|---|
| 5,346 | `YIELD_FROM` | cold |
| 1,712 | `CALL_ATTR_STAR` | cold |
| 1,417 | `PUSH_WIND` | cold |
| 1,348 | `CALL_VAR_ATTR_STAR` | cold |
| 1,217 | `CALL_FAST_ATTR_STAR` | cold |
| 1,210 | `TAIL_CALL_STAR` | cold |
| 1,210 | `CALL_STAR` | cold |
| 1,106 | `BUILD_CLASS` | cold |
| 1,061 | `IMPORT_STAR` | cold |
| 1,051 | `PAIR_SPLICE` | cold |
| 1,027 | `COMBINE_ARGS` | cold |
| 998 | `CALL_FAST_STAR` | cold |
| 928 | `CALL_VAR_STAR` | cold |
| 918 | `CHECK_COMPARE_FAST` | hot |
| 887 | `BUILD_FUNCTION` | cold |
| 858 | `PUSH_NS` | hot |
| 848 | `PUSH_WITH` | cold |
| 832 | `PUSH_DYN_WITH` | cold |
| 809 | `FAST_ATTR_STORE` | hot |
| 805 | `UNPACK_STAR` | cold |
| 795 | `UNPACK_FAST` | cold |
| 745 | `IMPORT_FROM` | cold |
| 738 | `COMBINE_KWDS` | cold |
| 733 | `RAISE` | cold |
| 712 | `POP_SHIFT` | cold |
| 674 | `CHECK_COMPARE_VAR` | cold |
| 664 | `CALL_FAST_ATTR_NAMES` | cold |
| 647 | `ITER_NEXT` | hot |
| 647 | `FAST_ATTR_INPLACE` | hot |
| 636 | `CHECK_COMPARE` | hot |
| 595 | `OBJECT_FORMAT` | hot |
| 579 | `CALL_VAR_ATTR_NAMES` | cold |
| 571 | `FAST_INPLACE_CONST` | hot |
| 569 | `FAST2` | hot |
| 557 | `POP_FINALLY` | cold |
| 550 | `VAR_ATTR_STORE` | cold |
| 546 | `VAR_ATTR_INPLACE` | cold |
| 546 | `EXCEPTION_MATCH` | hot |
| 543 | `SWITCH_EQ_FAST_ATTR` | hot |
| 541 | `FAST_ATTR` | hot |
| 540 | `UNPACK_VAR` | cold |
| 539 | `PROPER` | hot |
| 538 | `ITEM_TAKE` | hot |
| 535 | `POP_PROMPT` | cold |
| 522 | `CALL_FAST_ATTR` | hot |
| 513 | `CDR` | hot |
| 513 | `CAR` | hot |
| 503 | `CALL_FAST_NAMES` | cold |
| 498 | `FAST_LIST_APPEND` | hot |
| 491 | `CALL_ATTR_NAMES` | cold |
| 490 | `PUSH_FOREACH` | hot |
| 474 | `PUSH_SHIFT` | hot |
| 474 | `FAST_ATTR_TAKE` | hot |
| 460 | `UNPACK` | hot |
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
| 426 | `CALL_VAR_ATTR` | cold |
| 423 | `FAST4` | hot |
| 420 | `BUILD_LIST_EXTEND` | hot |
| 417 | `ATTR_INPLACE` | cold |
| 411 | `VAR_ATTR` | cold |
| 410 | `OBJECT_COMPARE` | hot |
| 410 | `ATTR_TAKE` | hot |
| 402 | `CONST_ITEM_TAKE` | hot |
| 399 | `SWITCH_EQ` | hot |
| 399 | `CALL_VAR_NAMES` | cold |
| 395 | `EXCEPTION` | hot |
| 394 | `SWITCH_EQ_VAR` | cold |
| 387 | `FAST_NEXT` | cold |
| 384 | `CONS` | hot |
| 381 | `OBJECT_STR` | hot |
| 378 | `VAR_ATTR_TAKE` | cold |
| 378 | `FAST_ATTR_REF` | hot |
| 375 | `VAR4` | cold |
| 374 | `OBJECT_NEXT` | cold |
| 373 | `CALL_FAST` | hot |
| 371 | `VAR2` | cold |
| 370 | `CONTINUE` | hot |
| 370 | `BREAK` | hot |
| 351 | `CALL_ATTR` | cold |
| 349 | `CONST_ITEM_STORE` | hot |
| 349 | `ATTR_STORE` | hot |
| 346 | `OBJECT_COMPARE_FAST` | hot |
| 333 | `VAR_BIND` | cold |
| 330 | `BUILD_LIST_APPEND` | hot |
| 329 | `ITEM_CLEAR` | hot |
| 314 | `JUMP_OR` | hot |
| 314 | `JUMP_AND` | hot |
| 311 | `VAR_TAKE` | cold |
| 306 | `VAR_INPLACE_CONST` | cold |
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
| 275 | `CONST_ITEM_REF` | hot |
| 275 | `CONST_ITEM` | hot |
| 275 | `ATTR_REF` | hot |
| 275 | `ATTR` | hot |
| 274 | `OBJECT_TRUTH` | hot |
| 274 | `OBJECT_NOT` | hot |
| 273 | `POP_WIND` | hot |
| 267 | `OBJECT_LEN` | hot |
| 267 | `OBJECT_FORMAT_CONST` | hot |
| 261 | `POP_WITH` | hot |
| 259 | `VAR_ATTR_REF` | cold |
| 250 | `FAST_ATTR_CLEAR` | hot |
| 247 | `FAST_BIND` | hot |
| 245 | `DEREF` | hot |
| 239 | `OBJECT_TYPE` | hot |
| 237 | `OBJECT_REPR` | hot |
| 237 | `OBJECT_ITER` | hot |
| 237 | `OBJECT_DIR` | hot |
| 237 | `OBJECT_BIT_INVERT` | hot |
| 234 | `VAR_REF` | cold |
| 231 | `GLOBALS` | hot |
| 228 | `POP_TRY` | hot |
| 226 | `VAR_STORE` | cold |
| 221 | `FAST` | hot |
| 220 | `POP_FOREACH` | hot |
| 219 | `VAR_ITER` | hot |
| 218 | `BUILD_KWDS` | hot |
| 218 | `BUILD_ARGS` | hot |
| 210 | `IMPORT` | hot |
| 205 | `JUMP_TRUE` | hot |
| 205 | `JUMP_FALSE` | hot |
| 205 | `AWAIT` | hot |
| 204 | `POP_LOOP` | hot |
| 199 | `PUSH_LOOP` | hot |
| 191 | `FAST_STORE_CONST` | hot |
| 191 | `CAPTURE_CC` | hot |
| 186 | `POP_COLLECT` | hot |
| 183 | `RETURN` | hot |
| 182 | `CHECK_NIL` | hot |
| 178 | `PUSH_COLLECT` | hot |
| 178 | `CONST_ITEM_CLEAR` | hot |
| 178 | `ATTR_CLEAR` | hot |
| 173 | `PUSH_CC` | hot |
| 173 | `BUILD_LIST_APPEND_CONST` | hot |
| 169 | `THEEND` | cold |
| 167 | `YIELD` | hot |
| 164 | `CALL` | hot |
| 162 | `VAR_ATTR_CLEAR` | cold |
| 162 | `VAR` | cold |
| 162 | `PARAM` | hot |
| 159 | `BUILD_LIST_CONST` | hot |
| 156 | `TAIL_CALL` | hot |
| 154 | `FAST_REF` | hot |
| 154 | `CONST` | hot |
| 151 | `VAR_CLEAR` | cold |
| 146 | `PUSH_PARAM` | hot |
| 146 | `FAST_TAKE` | hot |
| 141 | `POP` | hot |
| 124 | `VAR_INPLACE` | cold |
| 119 | `PUSH_PROMPT` | hot |
| 103 | `VAR_STORE_CONST` | cold |
| 103 | `LOCALS` | hot |
| 101 | `CLEAR_LOOP` | hot |
| 101 | `CLEAR_FOREACH` | hot |
| 88 | `CLEAR_TRY` | hot |
| 88 | `CLEAR_FINALLY` | hot |
| 78 | `FAST_INPLACE` | hot |
| 76 | `PUSH_TRY` | hot |
| 76 | `PUSH_FINALLY` | hot |
| 58 | `OBJECT_OPERAND` | hot |
| 58 | `OBJECT_INPLACE` | cold |
| 56 | `FAST_CLEAR` | hot |
| 56 | `DUP` | hot |
| 52 | `EXCEPTION_CLEAR` | hot |
| 48 | `POP_PARAM` | hot |
| 48 | `POP_NS` | hot |
| 48 | `POP_CC` | hot |
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
     row (diff-friendly). Regenerate the emission view alongside:
     `tox -qe opgrams`. See Methodology. -->
