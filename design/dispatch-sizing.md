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
shrink it) -- good enough to rank and prioritise.  Hotness is reasoned, not
profiled: there is no PGO/instrumented build yet (deferred until we have
representative profiling apps -- scheduler, koslife, churns, mcp, repl, dis_ng).

Measured 2026-07-12 on branch `eval_fast_deref`.

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| fully-inlined dispatch fn | 120,944 | current `koskari_eval_dispatch_computed_goto` |
| floor (all 242 handlers un-inlined) | 30,929 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 242 + loop scaffold |
| handler bodies | 85,406 | sum of inlined bodies -- the carving budget |

Distribution (top-heavy, long-tailed):

- `>= 1000 B`: 11 handlers, 18,251 B (21% of body)
- `>=  500 B`: 53 handlers, 49,160 B (58% of body)
- `>=  200 B`: 124 handlers, 72,361 B (85% of body)
- `>=  100 B`: 183 handlers, 81,860 B (96% of body)

## Clusters (size x reasoned hotness x structure)

| cluster | bytes (handlers) | % body | hotness | action |
|---|---:|---:|---|---|
| `*_OBJECT_*` arithmetic | 22,331 (78) | 26.1% | warm | share fast helper + slim stub |
| `CALL_*` family | 15,122 (17) | 17.7% | mixed | extract marshalling; keep fast call inline, un-inline STAR/NAMES |
| YIELD_FROM | 6,283 (1) | 7.4% | cold | un-inline / decompose |
| IMPORT_* | 2,016 (3) | 2.4% | cold | un-inline |

## Planned phases

- **A -- un-inline cold giants**: YIELD_FROM, IMPORT_*, CALL_*_STAR/NAMES,
  PUSH_WIND, POP_PROMPT/SHIFT, PAIR_SPLICE, TAIL_CALL_STAR, UNPACK_STAR,
  COMBINE_ARGS.  Mark `KOS_NOINLINE`.  Biggest, safest.
- **B -- share arithmetic families**: one fast helper parameterised by op, slim
  inline stubs.  Warm -- measure carefully.
- **C -- decompose CALL_FAST_ATTR / CALL_FAST**: keep fast call path inline,
  extract the rest.

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Full per-handler sizes (bytes, descending)

| bytes | handler | | bytes | handler |
|---:|---|---|---:|---|
| 6,283 | `YIELD_FROM` | | 210 | `IMPORT` |
| 1,422 | `NEXT` | | 203 | `VAR_REF` |
| 1,370 | `CALL_ATTR_STAR` | | 203 | `TAIL_CALL` |
| 1,292 | `CALL_VAR_ATTR_STAR` | | 191 | `FAST_STORE_CONST` |
| 1,271 | `PUSH_WIND` | | 189 | `OBJECT_TRUTH` |
| 1,186 | `CALL_VAR_ATTR_NAMES` | | 189 | `OBJECT_NOT` |
| 1,154 | `CALL_FAST_ATTR` | | 188 | `POP_FOREACH` |
| 1,090 | `CALL_FAST_ATTR_STAR` | | 186 | `OBJECT_VARS` |
| 1,077 | `CALL_FAST_ATTR_NAMES` | | 186 | `OBJECT_FORMAT_CONST` |
| 1,061 | `IMPORT_STAR` | | 186 | `CONST_ITEM_REF` |
| 1,045 | `CALL_VAR_ATTR` | | 186 | `CONST_ITEM` |
| 977 | `FAST_OBJECT_INPLACE_BIT_LEFT` | | 186 | `ATTR_REF` |
| 940 | `FAST_OBJECT_INPLACE_MODULO` | | 186 | `ATTR` |
| 940 | `FAST_OBJECT_INPLACE_BIT_RIGHT` | | 184 | `FAST_BIND` |
| 895 | `FAST_OBJECT_INPLACE_SUBTRACT` | | 181 | `OBJECT_NE` |
| 895 | `FAST_OBJECT_INPLACE_MULTIPLY` | | 181 | `OBJECT_LT` |
| 895 | `FAST_OBJECT_INPLACE_BIT_XOR` | | 181 | `OBJECT_LE` |
| 895 | `FAST_OBJECT_INPLACE_BIT_OR` | | 181 | `OBJECT_GT` |
| 895 | `FAST_OBJECT_INPLACE_BIT_AND` | | 181 | `OBJECT_GE` |
| 895 | `FAST_OBJECT_INPLACE_ADD` | | 181 | `OBJECT_EQ` |
| 866 | `BUILD_CLASS` | | 181 | `FAST` |
| 852 | `PAIR_SPLICE` | | 179 | `POP_WIND` |
| 823 | `CALL_FAST_STAR` | | 178 | `VAR_STORE` |
| 792 | `PUSH_WITH` | | 178 | `PUSH_COLLECT` |
| 792 | `PUSH_DYN_WITH` | | 175 | `OBJECT_LEN` |
| 775 | `TAIL_CALL_STAR` | | 173 | `PUSH_CC` |
| 775 | `CALL_STAR` | | 172 | `POP_LOOP` |
| 775 | `BUILD_FUNCTION` | | 172 | `BUILD_LIST` |
| 773 | `UNPACK_FAST` | | 170 | `POP_TRY` |
| 746 | `CALL_FAST_NAMES` | | 170 | `BUILD_KWDS` |
| 745 | `IMPORT_FROM` | | 170 | `BUILD_ARGS` |
| 732 | `CALL_VAR_STAR` | | 169 | `THEEND` |
| 732 | `CALL_ATTR_NAMES` | | 167 | `GLOBALS` |
| 730 | `CALL_ATTR` | | 167 | `DEREF` |
| 688 | `POP_PROMPT` | | 162 | `VAR_ATTR_CLEAR` |
| 666 | `RAISE` | | 162 | `POP_COLLECT` |
| 664 | `POP_SHIFT` | | 162 | `OBJECT_TYPE` |
| 662 | `PUSH_NS` | | 159 | `OBJECT_REPR` |
| 661 | `CALL_FAST` | | 159 | `OBJECT_ITER` |
| 655 | `VAR_OBJECT_INPLACE_MODULO` | | 159 | `OBJECT_DIR` |
| 653 | `UNPACK_STAR` | | 159 | `OBJECT_BIT_INVERT` |
| 653 | `FAST_ATTR_STORE` | | 156 | `AWAIT` |
| 651 | `ITER_NEXT` | | 154 | `PUSH_PARAM` |
| 638 | `CALL_VAR_NAMES` | | 151 | `VAR_CLEAR` |
| 627 | `FAST_OBJECT_INPLACE_POWER` | | 151 | `SWITCH_EQ_CONST` |
| 611 | `FAST_OBJECT_INPLACE_TRUE_DIVIDE` | | 151 | `JUMP_TRUE` |
| 611 | `FAST_OBJECT_INPLACE_MATRIX_MULTIPLY` | | 151 | `JUMP_FALSE` |
| 611 | `FAST_OBJECT_INPLACE_FLOOR_DIVIDE` | | 151 | `CAPTURE_CC` |
| 602 | `CALL_VAR` | | 150 | `CHECK_NIL` |
| 557 | `COMBINE_ARGS` | | 146 | `ATTR_CLEAR` |
| 549 | `PUSH_FOREACH` | | 143 | `SWITCH_EQ` |
| 512 | `VAR_ATTR_STORE` | | 138 | `CONST_ITEM_CLEAR` |
| 503 | `UNPACK_VAR` | | 135 | `PUSH_PROMPT` |
| 498 | `OBJECT_POWER` | | 131 | `YIELD` |
| 497 | `RETURN` | | 125 | `VAR` |
| 493 | `POP_FINALLY` | | 125 | `PARAM` |
| 492 | `FAST_ATTR` | | 124 | `CONST` |
| 469 | `CALL_NAMES` | | 117 | `FAST_REF` |
| 458 | `CALL` | | 109 | `FAST_TAKE` |
| 450 | `FAST_LIST_APPEND` | | 103 | `VAR_STORE_CONST` |
| 442 | `EXCEPTION_MATCH` | | 101 | `CLEAR_LOOP` |
| 429 | `PUSH_SHIFT` | | 101 | `CLEAR_FOREACH` |
| 423 | `CDR` | | 88 | `CLEAR_TRY` |
| 423 | `CAR` | | 88 | `CLEAR_FINALLY` |
| 418 | `SWITCH_EQ_FAST_ATTR` | | 79 | `LOCALS` |
| 413 | `FAST_ATTR_TAKE` | | 77 | `POP` |
| 389 | `VAR_LIST_APPEND` | | 76 | `PUSH_TRY` |
| 378 | `CHECK_EXCEPTION` | | 76 | `PUSH_FINALLY` |
| 375 | `UNPACK` | | 64 | `OBJECT_SUBTRACT` |
| 372 | `VAR_ATTR` | | 64 | `OBJECT_MULTIPLY` |
| 365 | `ITEM_TAKE` | | 64 | `OBJECT_MODULO` |
| 362 | `VAR_OBJECT_INPLACE_POWER` | | 64 | `OBJECT_INPLACE_MODULO` |
| 361 | `OBJECT_FORMAT` | | 64 | `OBJECT_BIT_XOR` |
| 357 | `POP_DYN_WITH` | | 64 | `OBJECT_BIT_RIGHT` |
| 357 | `OBJECT_IS_SUBCLASS` | | 64 | `OBJECT_BIT_OR` |
| 357 | `OBJECT_IS_INSTANCE` | | 64 | `OBJECT_BIT_LEFT` |
| 354 | `SWITCH_EQ_VAR_ATTR` | | 64 | `OBJECT_BIT_AND` |
| 346 | `VAR_OBJECT_INPLACE_TRUE_DIVIDE` | | 63 | `OBJECT_TRUE_DIVIDE` |
| 346 | `VAR_OBJECT_INPLACE_SUBTRACT` | | 63 | `OBJECT_STR` |
| 346 | `VAR_OBJECT_INPLACE_MULTIPLY` | | 63 | `OBJECT_MATRIX_MULTIPLY` |
| 346 | `VAR_OBJECT_INPLACE_MATRIX_MULTIPLY` | | 63 | `OBJECT_INPLACE_TRUE_DIVIDE` |
| 346 | `VAR_OBJECT_INPLACE_FLOOR_DIVIDE` | | 63 | `OBJECT_INPLACE_SUBTRACT` |
| 346 | `VAR_OBJECT_INPLACE_BIT_XOR` | | 63 | `OBJECT_INPLACE_POWER` |
| 346 | `VAR_OBJECT_INPLACE_BIT_RIGHT` | | 63 | `OBJECT_INPLACE_MULTIPLY` |
| 346 | `VAR_OBJECT_INPLACE_BIT_OR` | | 63 | `OBJECT_INPLACE_MATRIX_MULTIPLY` |
| 346 | `VAR_OBJECT_INPLACE_BIT_LEFT` | | 63 | `OBJECT_INPLACE_FLOOR_DIVIDE` |
| 346 | `VAR_OBJECT_INPLACE_BIT_AND` | | 63 | `OBJECT_INPLACE_BIT_XOR` |
| 346 | `VAR_OBJECT_INPLACE_ADD` | | 63 | `OBJECT_INPLACE_BIT_RIGHT` |
| 338 | `SWITCH_EQ_FAST` | | 63 | `OBJECT_INPLACE_BIT_OR` |
| 331 | `CONTINUE` | | 63 | `OBJECT_INPLACE_BIT_LEFT` |
| 331 | `BREAK` | | 63 | `OBJECT_INPLACE_BIT_AND` |
| 330 | `VAR_ATTR_TAKE` | | 63 | `OBJECT_INPLACE_ADD` |
| 330 | `EXCEPTION` | | 63 | `OBJECT_FLOOR_DIVIDE` |
| 330 | `COMBINE_KWDS` | | 62 | `CHECK_NE` |
| 314 | `ATTR_TAKE` | | 62 | `CHECK_LE` |
| 298 | `FAST_ATTR_REF` | | 62 | `CHECK_GT` |
| 298 | `CONST_ITEM_TAKE` | | 62 | `CHECK_GE` |
| 295 | `OBJECT_DIVMOD` | | 62 | `CHECK_EQ` |
| 290 | `BUILD_LIST_APPEND` | | 61 | `OBJECT_ADD` |
| 285 | `VAR_BIND` | | 60 | `CHECK_IS_SUBCLASS` |
| 282 | `ITEM` | | 60 | `CHECK_IS_INSTANCE` |
| 281 | `TAIL_CALL_NAMES` | | 59 | `OBJECT_NOT_IN` |
| 277 | `ITEM_REF` | | 59 | `OBJECT_IS_NOT` |
| 276 | `VAR_TAKE` | | 59 | `OBJECT_IS` |
| 266 | `SWITCH_EQ_VAR` | | 59 | `OBJECT_IN` |
| 263 | `OBJECT_AITER` | | 59 | `CHECK_LT` |
| 261 | `POP_WITH` | | 58 | `CHECK_NOT_IN` |
| 250 | `FAST_STORE` | | 58 | `CHECK_IS_NOT` |
| 250 | `FAST_ATTR_CLEAR` | | 56 | `FAST_CLEAR` |
| 242 | `ITEM_STORE` | | 56 | `DUP` |
| 242 | `CONST_ITEM_STORE` | | 55 | `CHECK_IS` |
| 242 | `ATTR_STORE` | | 55 | `CHECK_IN` |
| 223 | `BUILD_KWDS_NAMES` | | 52 | `POP_PARAM` |
| 221 | `ITEM_CLEAR` | | 52 | `POP_NS` |
| 218 | `VAR_ATTR_REF` | | 52 | `POP_CC` |
| 218 | `CONS` | | 52 | `EXCEPTION_CLEAR` |
| 215 | `PUSH_LOOP` | | 35 | `JUMP` |
| 214 | `JUMP_OR` | | 28 | `POSITION` |
| 214 | `JUMP_AND` | | 28 | `NOOP` |
| 212 | `PROPER` | | 28 | `LABEL` |
| 210 | `NILP` | | 27 | `GLOBALS.cold` |

<!-- Regenerate: throwaway build with KOS_OP_INLINE = static KOS_NOINLINE, then
     nm --print-size on the .so, grep ' t op_'. See Methodology. -->
