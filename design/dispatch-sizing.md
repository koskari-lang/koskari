# Dispatch function sizing baseline

Internal analysis for the effort to shrink `koskari_eval_dispatch_computed_goto`,
the single computed-goto dispatch function into which every opcode handler is
`always_inline`d.  As the opcode set grows, each handler's compiled size is
additive I-cache cost in that one function.  Goal: keep the hottest short paths
inline, migrate everything else one or two calls out.

## Methodology

Per-handler sizes come from a throwaway measurement build with `KOS_OP_INLINE`
redefined to `static KOS_NOINLINE`, so each `op_<NAME>` becomes its own symbol;
sizes read via `nm --print-size` on the built extension.  Standalone noinline
size is a proxy for a handler's inlined contribution (inlining can specialise or
shrink it) -- good enough to rank and prioritise.  The proxy still over-estimates:
the fully-inlined dispatch fn (58,433 B) is smaller than floor + bodies
(101,996 B), so at `-O2 -flto` inlining shares tails and specialises away more
than the standalone bodies imply.  Note that 67 handlers are now
`KOS_OP_HANDLER_COLD` -- their bodies appear in the table below but no longer
contribute to the inlined function at all.  Hotness is reasoned, not profiled:
there is no PGO/instrumented build yet (deferred until we have representative
profiling apps -- scheduler, koslife, churns, mcp, repl, dis).

Measured 2026-07-19 on `master` (post yield-from decompose, PR #104),
186 opcodes (one handler each; 187 symbols counting the split-out
`GLOBALS.cold`).

## Changes since the 2026-07-17 measurement

PR #104 (yield-from-decompose) landed three pieces: a second cold sweep
of opcode handlers (dispatch phase A2, 67 handlers now cold), the
generic capability-keyed iterator-boundary dispatcher, and the
decomposition of the `YIELD_FROM` drive family with an `ITER_NEXT`
hot/cold split.  `YIELD_FROM` itself is now cold and its standalone
body shrank 6,728 -> 5,346 B; `ITER_NEXT` 705 -> 615 B.  Net: the
fully-inlined dispatch fn dropped 65,435 -> 58,433 B (-10.7%) on a
near-constant body total (80,844 -> 79,356 B) -- the win is almost
entirely the A2 cold migration.  Cumulative from the 2026-07-12
baseline: 120,944 -> 58,433 B (-51.7%).

## Changes since the 2026-07-15 measurement

Three efforts landed between those measurements:

- **Comparison unification** (see
  [archive/compare-unification.md](archive/compare-unification.md)):
  the 24 per-comparison opcodes (12 `CHECK_*`, 12 `OBJECT_*`) collapsed
  into 6 selector-keyed handlers -- generic `CHECK_COMPARE` /
  `OBJECT_COMPARE` plus the fused `_VAR`/`_FAST` pairs.  Family bodies
  4,273 -> 2,642 B, and the two hot fused handlers (`CHECK_COMPARE_FAST`
  679 B, `OBJECT_COMPARE_FAST` 362 B) were born inline: every loop head
  is one dispatch and the bounds idiom is one or two.
- **Bulk local loads** (var4, PR #102): `VAR2`/`VAR4`/`FAST2`/`FAST4`
  added (1,738 B of bodies, the FAST pair inline) collapsing FAST runs
  corpus-wide.
- **VAR-family cold sweep + never-reached handlers**: 16 VAR-form
  handlers and the 4 never-reached metadata handlers went
  `KOS_OP_HANDLER_COLD`.

Net for that span: 70,962 -> 65,435 B while gaining eight opcodes.

## Changes since 2026-07-12

The prior baseline (120,944 B, 242 handlers, branch `eval_fast_deref`) predates
the arithmetic/in-place opcode consolidation.  The per-operator families that
dominated that measurement -- `OBJECT_ADD`/`OBJECT_SUBTRACT`/..., the ~78-handler
`*_OBJECT_INPLACE_*` block at 22,331 B -- have collapsed into a handful of
op-parameterised handlers (`OBJECT_OPERAND`, `OBJECT_INPLACE`,
`FAST_OBJECT_INPLACE`, `VAR_OBJECT_INPLACE`, plus the `*_ATTR_INPLACE` trio).
That is effectively **Phase B landed as opcode design** rather than as inline-stub
carving.  The call-unification effort then converted the CALL family onto the
shared noinline `kos_call_invoke` core (family bodies 21,340 -> 14,577 B;
dispatch fn 81,418 -> 70,962 B).  The comparison family followed the same
selector-keyed consolidation pattern, and the A2 cold sweep carved the
remaining cold giants out of the inline body.

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 58,433 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 186 handlers un-inlined) | 22,640 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 186 + loop scaffold |
| handler bodies | 79,356 | sum of standalone noinline bodies -- 43,684 B across the 67 cold handlers, 35,672 B across the 119 hot ones (the actual inline budget) |

Distribution (top-heavy, long-tailed; % of the 79,356 B body):

- `>= 1000 B`: 13 handlers, 20,809 B (26% of body)
- `>=  500 B`: 47 handlers, 43,010 B (54% of body)
- `>=  200 B`: 140 handlers, 73,884 B (93% of body)
- `>=  100 B`: 169 handlers, 78,387 B (99% of body)

## Clusters (size x reasoned hotness x structure)

| cluster | bytes (handlers) | % body | hotness | action |
|---|---:|---:|---|---|
| `CALL_*` family (incl. `TAIL_CALL*`) | 14,577 (21) | 18.4% | mixed | DONE (call-unification): prologues + shared core; residual is the 7 STAR gathers (8,868 B), kept by design |
| YIELD_FROM | 5,346 (1) | 6.7% | cold | DONE (PR #104): decomposed and cold |
| comparison family (`*_COMPARE*`) | 2,642 (6) | 3.3% | hot | DONE (compare-unification): selector-keyed, fused pairs born inline, VAR twins cold |
| IMPORT_* | 2,016 (3) | 2.5% | cold | cold (A2) |
| bulk loads (`VAR2/4`, `FAST2/4`) | 1,738 (4) | 2.2% | hot | DONE (var4): FAST pair inline, VAR twins cold |
| `*_ATTR_INPLACE` | 1,594 (3) | 2.0% | warm | already op-parameterised; leave inline |
| `*_OBJECT_*` arith/operand | 326 (4) | 0.4% | warm | already consolidated (Phase B) -- keep inline |

## Planned phases

- **A -- un-inline cold giants**: DONE across two sweeps; the A2 sweep
  (PR #104) has 67 handlers cold, including YIELD_FROM, IMPORT_*, the
  STAR gathers, PUSH_WIND, and the wider cold tail.
- **B -- share arithmetic families**: DONE.  Landed as opcode consolidation
  (`OBJECT_OPERAND`/`OBJECT_INPLACE`/`FAST_OBJECT_INPLACE`/`VAR_OBJECT_INPLACE`
  and the `*_ATTR_INPLACE` trio) rather than as slim inline stubs over a shared
  helper.
- **C -- decompose CALL_FAST_ATTR / CALL_FAST**: DONE, subsumed by the
  call-unification prologue/core split (kos_call_invoke); see
  [interpreter/call-unification.md](interpreter/call-unification.md).
- **D -- selector-keyed comparisons**: DONE, landed as compare-unification;
  see [archive/compare-unification.md](archive/compare-unification.md).

With A-D landed, the remaining levers are PGO
([interpreter/pgo-build.md](interpreter/pgo-build.md)) and further
opcode-design consolidation (operand fusion) rather than carving.
Remeasure dispatch fn size and run the suite + ASan after each batch.

## Emission n-grams (opgrams, same-day corpus)

The sizing view above says where inline bytes live; the emission view
says which *sequences* the compiler produces -- the fusion targets.
Regenerate the two together (`tox -qe opgrams`, corpus here: 102 codes
/ 771 blocks across bootstrap, cli, koslife, churn) so both are read
against the same tree.

Top weighted patterns (loop-depth weighted, base 100):

| weighted | raw | pattern | reading |
|---:|---:|---|---|
| 2.06M | 27 | `CONST FAST_OBJECT_INPLACE` | the `i += const` increment; with `... CONTINUE` (1.04M) it is the loop tail |
| 2.05M | 18 | `PUSH_LOOP CHECK_COMPARE_FAST` | the fused loop head -- compare-unification visible in emission |
| 1.13M | 45 | `OBJECT_OPERAND FAST_STORE` | compute-then-store; the fused store-destination quad target |
| 1.03M | 19 | `FAST2 ITEM` (and `FAST2 ITEM FAST_STORE`, 1.02M) | indexed access; a FAST-keyed ITEM fusion candidate |
| 1.02M | 68 | `CONST OBJECT_OPERAND` | operand feeder traffic still unfused |

Successor facts worth acting on: `FAST2 -> OBJECT_OPERAND` 20% (bulk
loads now feed arithmetic directly); `CALL_FAST_ATTR -> POP` 47% and
`CALL_FAST -> POP` 18% (the statement-call discard-bit idea);
`OBJECT_FORMAT -> CONST` 79% then `OBJECT_STR -> CALL_FAST_ATTR` 52%
(the interpolate-and-write idiom, string-template territory).

Read together with the sizing baseline, both views point the same way:
the carving phases (A-D) are done, and the remaining wins are
opcode-design consolidation -- the operand-fusion family (increment
tail, compute-store, operand feeders), item access, and the call
discard bit -- plus PGO for layout.

## Full per-handler sizes (bytes, descending)

Temperature is the handler macro: `hot` = `KOS_OP_HANDLER`
(always-inlined into the dispatch fn), `cold` = `KOS_OP_HANDLER_COLD`
(out of line; the body appears here but costs the inline fn nothing).

| bytes | handler | temp |
|---:|---|---|
| 5,346 | `YIELD_FROM` | cold |
| 1,845 | `NEXT` | cold |
| 1,712 | `CALL_ATTR_STAR` | cold |
| 1,493 | `CALL_VAR_ATTR_STAR` | cold |
| 1,417 | `PUSH_WIND` | cold |
| 1,301 | `CALL_FAST_ATTR_STAR` | cold |
| 1,210 | `CALL_STAR` | cold |
| 1,210 | `TAIL_CALL_STAR` | cold |
| 1,106 | `BUILD_CLASS` | cold |
| 1,061 | `IMPORT_STAR` | cold |
| 1,051 | `PAIR_SPLICE` | cold |
| 1,043 | `COMBINE_ARGS` | cold |
| 1,014 | `CALL_FAST_STAR` | cold |
| 928 | `CALL_VAR_STAR` | cold |
| 887 | `BUILD_FUNCTION` | cold |
| 848 | `PUSH_WITH` | cold |
| 842 | `PUSH_NS` | hot |
| 832 | `PUSH_DYN_WITH` | cold |
| 821 | `UNPACK_STAR` | cold |
| 811 | `UNPACK_FAST` | cold |
| 809 | `FAST_ATTR_STORE` | hot |
| 745 | `IMPORT_FROM` | cold |
| 738 | `COMBINE_KWDS` | cold |
| 733 | `RAISE` | cold |
| 728 | `POP_SHIFT` | cold |
| 680 | `CALL_FAST_ATTR_NAMES` | cold |
| 679 | `CHECK_COMPARE_FAST` | hot |
| 663 | `FAST_ATTR_INPLACE` | cold |
| 629 | `PUSH_FOREACH` | hot |
| 615 | `ITER_NEXT` | hot |
| 595 | `OBJECT_FORMAT` | hot |
| 579 | `CALL_VAR_ATTR_NAMES` | cold |
| 569 | `FAST2` | hot |
| 557 | `POP_FINALLY` | cold |
| 550 | `VAR_ATTR_STORE` | cold |
| 546 | `EXCEPTION_MATCH` | hot |
| 543 | `SWITCH_EQ_FAST_ATTR` | hot |
| 541 | `FAST_ATTR` | hot |
| 540 | `UNPACK_VAR` | cold |
| 539 | `PROPER` | hot |
| 538 | `ITEM_TAKE` | hot |
| 535 | `POP_PROMPT` | cold |
| 530 | `VAR_ATTR_INPLACE` | cold |
| 522 | `CALL_FAST_ATTR` | hot |
| 513 | `CAR` | hot |
| 513 | `CDR` | hot |
| 503 | `CALL_FAST_NAMES` | cold |
| 498 | `FAST_LIST_APPEND` | hot |
| 491 | `CALL_ATTR_NAMES` | cold |
| 474 | `FAST_ATTR_TAKE` | hot |
| 474 | `PUSH_SHIFT` | hot |
| 468 | `UNPACK` | hot |
| 467 | `CHECK_COMPARE_VAR` | cold |
| 460 | `SWITCH_EQ_VAR_ATTR` | cold |
| 450 | `OBJECT_DIVMOD` | cold |
| 449 | `POP_DYN_WITH` | hot |
| 447 | `CALL_NAMES` | cold |
| 445 | `BUILD_LIST_EXTEND_CONST` | hot |
| 445 | `ITEM` | hot |
| 445 | `VAR_LIST_APPEND` | cold |
| 442 | `ITEM_REF` | hot |
| 439 | `SWITCH_EQ_FAST` | hot |
| 437 | `CHECK_COMPARE` | hot |
| 429 | `CHECK_EXCEPTION` | hot |
| 426 | `CALL_VAR_ATTR` | cold |
| 423 | `FAST4` | hot |
| 420 | `BUILD_LIST_EXTEND` | hot |
| 411 | `VAR_ATTR` | cold |
| 410 | `ATTR_TAKE` | hot |
| 410 | `OBJECT_COMPARE` | hot |
| 402 | `CONST_ITEM_TAKE` | hot |
| 401 | `ATTR_INPLACE` | cold |
| 399 | `CALL_VAR_NAMES` | cold |
| 399 | `SWITCH_EQ` | hot |
| 395 | `EXCEPTION` | hot |
| 394 | `SWITCH_EQ_VAR` | cold |
| 389 | `CALL_FAST` | hot |
| 384 | `CONS` | hot |
| 381 | `OBJECT_STR` | hot |
| 378 | `FAST_ATTR_REF` | hot |
| 378 | `VAR_ATTR_TAKE` | cold |
| 375 | `VAR4` | cold |
| 371 | `VAR2` | cold |
| 370 | `BREAK` | hot |
| 370 | `CONTINUE` | hot |
| 362 | `OBJECT_COMPARE_FAST` | hot |
| 353 | `OBJECT_AITER` | hot |
| 351 | `CALL_ATTR` | cold |
| 349 | `ATTR_STORE` | hot |
| 349 | `CONST_ITEM_STORE` | hot |
| 333 | `VAR_BIND` | cold |
| 330 | `BUILD_LIST_APPEND` | hot |
| 329 | `ITEM_CLEAR` | hot |
| 314 | `JUMP_AND` | hot |
| 314 | `JUMP_OR` | hot |
| 311 | `VAR_TAKE` | cold |
| 301 | `FAST_STORE` | hot |
| 301 | `TAIL_CALL_NAMES` | cold |
| 296 | `ITEM_STORE` | hot |
| 293 | `BUILD_LIST` | hot |
| 293 | `CALL_VAR` | cold |
| 287 | `NILP` | hot |
| 287 | `OBJECT_COMPARE_VAR` | cold |
| 285 | `OBJECT_VARS` | hot |
| 279 | `SWITCH_EQ_CONST` | hot |
| 277 | `BUILD_KWDS_NAMES` | hot |
| 275 | `ATTR` | hot |
| 275 | `ATTR_REF` | hot |
| 275 | `CONST_ITEM` | hot |
| 275 | `CONST_ITEM_REF` | hot |
| 275 | `OBJECT_FORMAT_CONST` | hot |
| 274 | `OBJECT_NOT` | hot |
| 274 | `OBJECT_TRUTH` | hot |
| 273 | `POP_WIND` | hot |
| 267 | `OBJECT_LEN` | hot |
| 261 | `POP_WITH` | hot |
| 259 | `VAR_ATTR_REF` | cold |
| 253 | `OBJECT_BIT_INVERT` | hot |
| 253 | `OBJECT_DIR` | hot |
| 253 | `OBJECT_ITER` | hot |
| 253 | `OBJECT_REPR` | hot |
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
| 218 | `BUILD_ARGS` | hot |
| 218 | `BUILD_KWDS` | hot |
| 215 | `PUSH_LOOP` | hot |
| 210 | `IMPORT` | hot |
| 205 | `AWAIT` | hot |
| 205 | `JUMP_FALSE` | hot |
| 205 | `JUMP_TRUE` | hot |
| 204 | `POP_LOOP` | hot |
| 202 | `POP_COLLECT` | hot |
| 191 | `CAPTURE_CC` | hot |
| 191 | `FAST_STORE_CONST` | hot |
| 183 | `RETURN` | hot |
| 182 | `CHECK_NIL` | hot |
| 178 | `ATTR_CLEAR` | hot |
| 178 | `CONST_ITEM_CLEAR` | hot |
| 178 | `PUSH_COLLECT` | hot |
| 173 | `BUILD_LIST_APPEND_CONST` | hot |
| 173 | `PUSH_CC` | hot |
| 169 | `THEEND` | cold |
| 167 | `YIELD` | hot |
| 164 | `CALL` | hot |
| 164 | `TAIL_CALL` | hot |
| 162 | `PARAM` | hot |
| 162 | `VAR` | cold |
| 162 | `VAR_ATTR_CLEAR` | cold |
| 159 | `BUILD_LIST_CONST` | hot |
| 154 | `CONST` | hot |
| 154 | `FAST_REF` | hot |
| 154 | `PUSH_PARAM` | hot |
| 151 | `VAR_CLEAR` | cold |
| 146 | `FAST_TAKE` | hot |
| 141 | `POP` | hot |
| 135 | `PUSH_PROMPT` | hot |
| 124 | `VAR_OBJECT_INPLACE` | cold |
| 103 | `LOCALS` | hot |
| 103 | `VAR_STORE_CONST` | cold |
| 101 | `CLEAR_FOREACH` | hot |
| 101 | `CLEAR_LOOP` | hot |
| 88 | `CLEAR_FINALLY` | hot |
| 88 | `CLEAR_TRY` | hot |
| 78 | `FAST_OBJECT_INPLACE` | cold |
| 76 | `PUSH_FINALLY` | hot |
| 76 | `PUSH_TRY` | hot |
| 62 | `OBJECT_INPLACE` | cold |
| 62 | `OBJECT_OPERAND` | cold |
| 56 | `DUP` | hot |
| 56 | `FAST_CLEAR` | hot |
| 52 | `EXCEPTION_CLEAR` | hot |
| 52 | `POP_CC` | hot |
| 52 | `POP_NS` | hot |
| 52 | `POP_PARAM` | hot |
| 35 | `JUMP` | hot |
| 28 | `LABEL` | cold |
| 28 | `NOOP` | cold |
| 28 | `POSITION` | cold |

<!-- Regenerate: throwaway build with KOS_OP_INLINE = static KOS_NOINLINE (edit
     koskari/lib/hints.h), rebuild with `uv pip install -qe .`, then
     `nm --print-size --radix=d` on the .so, grep ' t op_[A-Z]'. Dispatch fn
     size and the noinline floor are the `koskari_eval_dispatch_computed_goto`
     symbol in the inlined and noinline builds respectively. Restore hints.h and
     rebuild afterward. Table is two columns, one opcode per row (diff-friendly).
     Regenerate the emission view alongside: `tox -qe opgrams`. See
     Methodology. -->
