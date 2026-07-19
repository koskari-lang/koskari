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
the fully-inlined dispatch fn (59,349 B) is smaller than floor + bodies
(103,121 B), so at `-O2 -flto` inlining shares tails and specialises away more
than the standalone bodies imply.  68 handlers are
`KOS_OP_HANDLER_COLD` -- their bodies appear in the table below but do not
contribute to the inlined function at all.  Hotness is reasoned, not profiled:
there is no PGO/instrumented build yet (deferred until we have representative
profiling apps -- scheduler, koslife, churns, mcp, repl, dis).

Measured 2026-07-19 on `master`, 188 opcodes (one handler each; 189
symbols counting the split-out `GLOBALS.cold`).

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 59,349 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 188 handlers un-inlined) | 22,872 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 188 + loop scaffold |
| handler bodies | 80,249 | sum of standalone noinline bodies -- 43,990 B across the 68 cold handlers, 36,259 B across the 120 hot ones (the actual inline budget) |

Distribution (top-heavy, long-tailed; % of the 80,249 B body):

- `>= 1000 B`: 13 handlers, 20,809 B (26% of body)
- `>=  500 B`: 48 handlers, 43,597 B (54% of body)
- `>=  200 B`: 142 handlers, 74,777 B (93% of body)
- `>=  100 B`: 171 handlers, 79,280 B (99% of body)

## Remaining levers

- **PGO** ([interpreter/pgo-build.md](interpreter/pgo-build.md)) for
  layout of the inline body.
- **Opcode-design consolidation** (operand fusion, item access) rather
  than further carving -- see the emission view below for the targets.

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Emission n-grams (opgrams, same-day corpus)

The sizing view above says where inline bytes live; the emission view
says which *sequences* the compiler produces -- the fusion targets.
Regenerate the two together (`tox -qe opgrams`, corpus here: 103 codes
/ 778 blocks across bootstrap, cli, koslife, churn) so both are read
against the same tree.

Top weighted patterns (loop-depth weighted, base 100):

| weighted | raw | pattern | reading |
|---:|---:|---|---|
| 2.05M | 18 | `PUSH_LOOP CHECK_COMPARE_FAST` | the fused loop head |
| 2.01M | 3 | `FAST4 ITEM` (and `FAST2 ITEM`, 1.03M) | indexed access off bulk loads; a FAST-keyed ITEM fusion candidate |
| 2.00M | 2 | `ITEM ITEM_STORE` | element move between subscripts |
| 1.13M | 45 | `OBJECT_OPERAND FAST_STORE` | compute-then-store; the fused store-destination quad target |
| 1.04M | 19 | `FAST_INPLACE_CONST CONTINUE` | the loop tail: constant increment in one dispatch |
| 1.02M | 68 | `CONST OBJECT_OPERAND` | operand feeder traffic still unfused |

Successor facts worth acting on: `FAST2 -> OBJECT_OPERAND` 20% (bulk
loads feed arithmetic directly); `OBJECT_FORMAT -> CONST` 79% then
`OBJECT_STR -> CALL_FAST_ATTR` 52% (the interpolate-and-write idiom,
string-template territory).

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
| 858 | `PUSH_NS` | hot |
| 848 | `PUSH_WITH` | cold |
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
| 647 | `FAST_ATTR_INPLACE` | cold |
| 629 | `PUSH_FOREACH` | hot |
| 615 | `ITER_NEXT` | hot |
| 595 | `OBJECT_FORMAT` | hot |
| 579 | `CALL_VAR_ATTR_NAMES` | cold |
| 571 | `FAST_INPLACE_CONST` | hot |
| 569 | `FAST2` | hot |
| 557 | `POP_FINALLY` | cold |
| 550 | `VAR_ATTR_STORE` | cold |
| 546 | `EXCEPTION_MATCH` | hot |
| 546 | `VAR_ATTR_INPLACE` | cold |
| 543 | `SWITCH_EQ_FAST_ATTR` | hot |
| 541 | `FAST_ATTR` | hot |
| 540 | `UNPACK_VAR` | cold |
| 539 | `PROPER` | hot |
| 538 | `ITEM_TAKE` | hot |
| 535 | `POP_PROMPT` | cold |
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
| 306 | `VAR_INPLACE_CONST` | cold |
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
| 124 | `VAR_INPLACE` | cold |
| 103 | `LOCALS` | hot |
| 103 | `VAR_STORE_CONST` | cold |
| 101 | `CLEAR_FOREACH` | hot |
| 101 | `CLEAR_LOOP` | hot |
| 88 | `CLEAR_FINALLY` | hot |
| 88 | `CLEAR_TRY` | hot |
| 78 | `FAST_INPLACE` | cold |
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
