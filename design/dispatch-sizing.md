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
shrink it) -- good enough to rank and prioritise.  The proxy now over-estimates
noticeably: the fully-inlined dispatch fn (81,418 B) is smaller than floor +
bodies (111,984 B), so at `-O2 -flto` inlining shares tails and specialises away
more than the standalone bodies imply.  Hotness is reasoned, not profiled: there
is no PGO/instrumented build yet (deferred until we have representative profiling
apps -- scheduler, koslife, churns, mcp, repl, dis_ng).

Measured 2026-07-15 on branch `master`, 200 opcodes (one handler each; 201
symbols counting the split-out `GLOBALS.cold`).

## Changes since 2026-07-12

The prior baseline (120,944 B, 242 handlers, branch `eval_fast_deref`) predates
the arithmetic/in-place opcode consolidation.  The per-operator families that
dominated that measurement -- `OBJECT_ADD`/`OBJECT_SUBTRACT`/..., the ~78-handler
`*_OBJECT_INPLACE_*` block at 22,331 B -- have collapsed into a handful of
op-parameterised handlers (`OBJECT_OPERAND`, `OBJECT_INPLACE`,
`FAST_OBJECT_INPLACE`, `VAR_OBJECT_INPLACE`, plus the `*_ATTR_INPLACE` trio).
That is effectively **Phase B landed as opcode design** rather than as inline-stub
carving.  Net effect: the fully-inlined dispatch fn dropped 120,944 -> 81,418 B
(~33%).  New `BUILD_LIST_*`/`_CONST` variants were also added.  The `CALL_*`
family is now the dominant carving target.

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 81,418 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 200 handlers un-inlined) | 24,488 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 200 + loop scaffold |
| handler bodies | 87,496 | sum of standalone noinline bodies -- the carving budget |

Distribution (top-heavy, long-tailed; % of the 87,496 B body):

- `>= 1000 B`: 17 handlers, 27,289 B (31% of body)
- `>=  500 B`: 55 handlers, 53,950 B (62% of body)
- `>=  200 B`: 140 handlers, 81,374 B (93% of body)
- `>=  100 B`: 167 handlers, 85,549 B (98% of body)

## Clusters (size x reasoned hotness x structure)

| cluster | bytes (handlers) | % body | hotness | action |
|---|---:|---:|---|---|
| `CALL_*` family (incl. `TAIL_CALL*`) | 21,340 (21) | 24.4% | mixed | extract marshalling; keep fast call inline, un-inline STAR/NAMES/ATTR variants |
| YIELD_FROM | 6,760 (1) | 7.7% | cold | un-inline / decompose |
| IMPORT_* | 2,016 (3) | 2.3% | cold | un-inline |
| `*_ATTR_INPLACE` | 1,578 (3) | 1.8% | warm | already op-parameterised; leave inline |
| `*_OBJECT_*` arith/operand | 776 (5) | 0.9% | warm | already consolidated (Phase B) -- keep inline |

## Planned phases

- **A -- un-inline cold giants**: YIELD_FROM, IMPORT_*, CALL_*_STAR/NAMES/ATTR,
  PUSH_WIND, POP_PROMPT/SHIFT, PAIR_SPLICE, TAIL_CALL_STAR, UNPACK_STAR/FAST,
  COMBINE_ARGS, BUILD_CLASS, BUILD_FUNCTION.  Mark `KOS_NOINLINE`.  Biggest,
  safest.
- **B -- share arithmetic families**: DONE.  Landed as opcode consolidation
  (`OBJECT_OPERAND`/`OBJECT_INPLACE`/`FAST_OBJECT_INPLACE`/`VAR_OBJECT_INPLACE`
  and the `*_ATTR_INPLACE` trio) rather than as slim inline stubs over a shared
  helper.
- **C -- decompose CALL_FAST_ATTR / CALL_FAST**: keep fast call path inline,
  extract the rest.  Now the largest lever, since `CALL_*` is 24% of the body.

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Full per-handler sizes (bytes, descending)

| bytes | handler | | bytes | handler |
|---:|---|---|---:|---|
| 6,760 | `YIELD_FROM` | | 287 | `NILP` |
| 1,845 | `NEXT` | | 285 | `OBJECT_VARS` |
| 1,712 | `CALL_ATTR_STAR` | | 279 | `SWITCH_EQ_CONST` |
| 1,493 | `CALL_VAR_ATTR_STAR` | | 277 | `BUILD_KWDS_NAMES` |
| 1,458 | `CALL_VAR_ATTR_NAMES` | | 275 | `OBJECT_FORMAT_CONST` |
| 1,417 | `PUSH_WIND` | | 275 | `CONST_ITEM_REF` |
| 1,301 | `CALL_FAST_ATTR_STAR` | | 275 | `CONST_ITEM` |
| 1,277 | `CALL_VAR_ATTR` | | 275 | `ATTR_REF` |
| 1,210 | `TAIL_CALL_STAR` | | 275 | `ATTR` |
| 1,210 | `CALL_STAR` | | 274 | `OBJECT_TRUTH` |
| 1,210 | `CALL_FAST_ATTR` | | 274 | `OBJECT_NOT` |
| 1,137 | `CALL_FAST_ATTR_NAMES` | | 273 | `POP_WIND` |
| 1,106 | `BUILD_CLASS` | | 267 | `OBJECT_LEN` |
| 1,061 | `IMPORT_STAR` | | 261 | `POP_WITH` |
| 1,051 | `PAIR_SPLICE` | | 259 | `VAR_ATTR_REF` |
| 1,027 | `COMBINE_ARGS` | | 253 | `OBJECT_REPR` |
| 1,014 | `CALL_FAST_STAR` | | 253 | `OBJECT_ITER` |
| 986 | `CALL_FAST_NAMES` | | 253 | `OBJECT_DIR` |
| 962 | `CALL_ATTR` | | 253 | `OBJECT_BIT_INVERT` |
| 956 | `CALL_ATTR_NAMES` | | 250 | `FAST_ATTR_CLEAR` |
| 928 | `CALL_VAR_STAR` | | 247 | `FAST_BIND` |
| 887 | `BUILD_FUNCTION` | | 245 | `DEREF` |
| 869 | `CALL_FAST` | | 239 | `OBJECT_TYPE` |
| 848 | `PUSH_WITH` | | 236 | `POP_FOREACH` |
| 847 | `CALL_VAR` | | 234 | `VAR_REF` |
| 842 | `PUSH_NS` | | 228 | `POP_TRY` |
| 833 | `CALL_VAR_NAMES` | | 226 | `VAR_STORE` |
| 832 | `PUSH_DYN_WITH` | | 221 | `FAST` |
| 821 | `UNPACK_STAR` | | 218 | `BUILD_KWDS` |
| 811 | `UNPACK_FAST` | | 218 | `BUILD_ARGS` |
| 809 | `FAST_ATTR_STORE` | | 215 | `PUSH_LOOP` |
| 783 | `CALL_NAMES` | | 210 | `IMPORT` |
| 745 | `IMPORT_FROM` | | 205 | `JUMP_TRUE` |
| 738 | `COMBINE_KWDS` | | 205 | `JUMP_FALSE` |
| 733 | `RAISE` | | 205 | `AWAIT` |
| 728 | `POP_SHIFT` | | 204 | `POP_LOOP` |
| 705 | `ITER_NEXT` | | 204 | `GLOBALS` |
| 650 | `CALL` | | 203 | `TAIL_CALL` |
| 647 | `FAST_ATTR_INPLACE` | | 202 | `POP_COLLECT` |
| 629 | `PUSH_FOREACH` | | 191 | `FAST_STORE_CONST` |
| 595 | `OBJECT_FORMAT` | | 191 | `CAPTURE_CC` |
| 557 | `POP_FINALLY` | | 183 | `RETURN` |
| 550 | `VAR_ATTR_STORE` | | 182 | `CHECK_NIL` |
| 546 | `EXCEPTION_MATCH` | | 178 | `PUSH_COLLECT` |
| 543 | `SWITCH_EQ_FAST_ATTR` | | 178 | `CONST_ITEM_CLEAR` |
| 541 | `FAST_ATTR` | | 178 | `ATTR_CLEAR` |
| 540 | `UNPACK_VAR` | | 173 | `PUSH_CC` |
| 539 | `PROPER` | | 173 | `BUILD_LIST_APPEND_CONST` |
| 538 | `ITEM_TAKE` | | 169 | `THEEND` |
| 535 | `POP_PROMPT` | | 167 | `YIELD` |
| 530 | `VAR_ATTR_INPLACE` | | 162 | `VAR_ATTR_CLEAR` |
| 516 | `OBJECT_IS_SUBCLASS` | | 162 | `VAR` |
| 516 | `OBJECT_IS_INSTANCE` | | 162 | `PARAM` |
| 513 | `CDR` | | 159 | `BUILD_LIST_CONST` |
| 513 | `CAR` | | 154 | `PUSH_PARAM` |
| 498 | `FAST_LIST_APPEND` | | 154 | `FAST_REF` |
| 474 | `PUSH_SHIFT` | | 154 | `CONST` |
| 474 | `FAST_ATTR_TAKE` | | 151 | `VAR_CLEAR` |
| 468 | `UNPACK` | | 146 | `FAST_TAKE` |
| 460 | `SWITCH_EQ_VAR_ATTR` | | 141 | `POP` |
| 450 | `OBJECT_DIVMOD` | | 135 | `PUSH_PROMPT` |
| 449 | `POP_DYN_WITH` | | 124 | `VAR_OBJECT_INPLACE` |
| 445 | `VAR_LIST_APPEND` | | 103 | `VAR_STORE_CONST` |
| 445 | `ITEM` | | 103 | `LOCALS` |
| 445 | `BUILD_LIST_EXTEND_CONST` | | 101 | `CLEAR_LOOP` |
| 442 | `ITEM_REF` | | 101 | `CLEAR_FOREACH` |
| 439 | `SWITCH_EQ_FAST` | | 88 | `CLEAR_TRY` |
| 429 | `CHECK_EXCEPTION` | | 88 | `CLEAR_FINALLY` |
| 420 | `BUILD_LIST_EXTEND` | | 78 | `FAST_OBJECT_INPLACE` |
| 411 | `VAR_ATTR` | | 76 | `PUSH_TRY` |
| 410 | `ATTR_TAKE` | | 76 | `PUSH_FINALLY` |
| 402 | `CONST_ITEM_TAKE` | | 62 | `OBJECT_OPERAND` |
| 401 | `ATTR_INPLACE` | | 62 | `OBJECT_INPLACE` |
| 399 | `SWITCH_EQ` | | 62 | `CHECK_NE` |
| 397 | `OBJECT_STR` | | 62 | `CHECK_LE` |
| 395 | `EXCEPTION` | | 62 | `CHECK_GT` |
| 394 | `SWITCH_EQ_VAR` | | 62 | `CHECK_GE` |
| 384 | `CONS` | | 62 | `CHECK_EQ` |
| 381 | `OBJECT_NE` | | 60 | `CHECK_IS_SUBCLASS` |
| 381 | `OBJECT_LT` | | 60 | `CHECK_IS_INSTANCE` |
| 381 | `OBJECT_LE` | | 59 | `OBJECT_NOT_IN` |
| 381 | `OBJECT_GT` | | 59 | `OBJECT_IS_NOT` |
| 381 | `OBJECT_GE` | | 59 | `OBJECT_IS` |
| 381 | `OBJECT_EQ` | | 59 | `OBJECT_IN` |
| 378 | `VAR_ATTR_TAKE` | | 59 | `CHECK_LT` |
| 378 | `FAST_ATTR_REF` | | 58 | `CHECK_NOT_IN` |
| 370 | `CONTINUE` | | 58 | `CHECK_IS_NOT` |
| 370 | `BREAK` | | 56 | `FAST_CLEAR` |
| 353 | `OBJECT_AITER` | | 56 | `DUP` |
| 349 | `CONST_ITEM_STORE` | | 55 | `CHECK_IS` |
| 349 | `ATTR_STORE` | | 55 | `CHECK_IN` |
| 333 | `VAR_BIND` | | 52 | `POP_PARAM` |
| 330 | `BUILD_LIST_APPEND` | | 52 | `POP_NS` |
| 329 | `ITEM_CLEAR` | | 52 | `POP_CC` |
| 314 | `JUMP_OR` | | 52 | `EXCEPTION_CLEAR` |
| 314 | `JUMP_AND` | | 35 | `JUMP` |
| 311 | `VAR_TAKE` | | 28 | `POSITION` |
| 301 | `TAIL_CALL_NAMES` | | 28 | `NOOP` |
| 301 | `FAST_STORE` | | 28 | `LABEL` |
| 296 | `ITEM_STORE` | | 27 | `GLOBALS.cold` |
| 293 | `BUILD_LIST` | |  |  |

<!-- Regenerate: throwaway build with KOS_OP_INLINE = static KOS_NOINLINE (edit
     koskari/lib/hints.h), rebuild with `uv pip install -qe .`, then
     `nm --print-size --radix=d` on the .so, grep ' t op_[A-Z]'. Dispatch fn
     size and the noinline floor are the `koskari_eval_dispatch_computed_goto`
     symbol in the inlined and noinline builds respectively. Restore hints.h and
     rebuild afterward. See Methodology. -->
