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

Measured 2026-07-15 on branch `call_cleanups` (post call-unification
phases 1-3 + C1/C2), 200 opcodes (one handler each; 201 symbols counting
the split-out `GLOBALS.cold`).

## Changes since the 2026-07-15 master measurement

The call-unification effort (see
[interpreter/call-unification.md](interpreter/call-unification.md))
converted the CALL family onto the shared noinline `kos_call_invoke`
core: the 12 converted handlers plus TAIL_CALL collapsed to
gather-and-delegate prologues (CALL 650 -> 164 B, CALL_FAST 869 -> 389,
CALL_FAST_ATTR 1,210 -> 522), taking the family's standalone bodies
from 21,340 to 14,577 B -- of which the 7 kept-by-design STAR gathers
are 8,868 B.  The fully-inlined dispatch fn dropped 81,418 -> 70,962 B
(-12.8%).  `YIELD_FROM` (6,760 B) is now unambiguously the largest
single body; the Phase A cold-giant carving (YIELD_FROM, IMPORT_*,
PUSH_WIND, PAIR_SPLICE, ...) is the remaining lever.

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
| fully-inlined dispatch fn | 70,962 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`) |
| floor (all 200 handlers un-inlined) | 24,488 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 200 + loop scaffold |
| handler bodies | 80,765 | sum of standalone noinline bodies -- the carving budget |

Distribution (top-heavy, long-tailed; % of the 87,496 B body):

- `>= 1000 B`: 13 handlers, 22,223 B (27% of body)
- `>=  500 B`: 47 handlers, 44,298 B (54% of body)
- `>=  200 B`: 138 handlers, 74,315 B (92% of body)
- `>=  100 B`: 167 handlers, 78,818 B (97% of body)

## Clusters (size x reasoned hotness x structure)

| cluster | bytes (handlers) | % body | hotness | action |
|---|---:|---:|---|---|
| `CALL_*` family (incl. `TAIL_CALL*`) | 14,577 (21) | 18.0% | mixed | DONE (call-unification): prologues + shared core; residual is the 7 STAR gathers (8,868 B), kept by design |
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
- **C -- decompose CALL_FAST_ATTR / CALL_FAST**: DONE, subsumed by the
  call-unification prologue/core split (kos_call_invoke); see
  [interpreter/call-unification.md](interpreter/call-unification.md).

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Full per-handler sizes (bytes, descending)

| bytes | handler | | bytes | handler |
|---:|---|---|---:|---|
| 6,760 | `YIELD_FROM` | | 285 | `OBJECT_VARS` |
| 1,845 | `NEXT` | | 279 | `SWITCH_EQ_CONST` |
| 1,712 | `CALL_ATTR_STAR` | | 277 | `BUILD_KWDS_NAMES` |
| 1,493 | `CALL_VAR_ATTR_STAR` | | 275 | `ATTR` |
| 1,417 | `PUSH_WIND` | | 275 | `ATTR_REF` |
| 1,301 | `CALL_FAST_ATTR_STAR` | | 275 | `CONST_ITEM` |
| 1,210 | `CALL_STAR` | | 275 | `CONST_ITEM_REF` |
| 1,210 | `TAIL_CALL_STAR` | | 275 | `OBJECT_FORMAT_CONST` |
| 1,106 | `BUILD_CLASS` | | 274 | `OBJECT_NOT` |
| 1,061 | `IMPORT_STAR` | | 274 | `OBJECT_TRUTH` |
| 1,051 | `PAIR_SPLICE` | | 273 | `POP_WIND` |
| 1,043 | `COMBINE_ARGS` | | 267 | `OBJECT_LEN` |
| 1,014 | `CALL_FAST_STAR` | | 261 | `POP_WITH` |
| 928 | `CALL_VAR_STAR` | | 259 | `VAR_ATTR_REF` |
| 887 | `BUILD_FUNCTION` | | 253 | `OBJECT_BIT_INVERT` |
| 848 | `PUSH_WITH` | | 253 | `OBJECT_DIR` |
| 842 | `PUSH_NS` | | 253 | `OBJECT_ITER` |
| 832 | `PUSH_DYN_WITH` | | 253 | `OBJECT_REPR` |
| 821 | `UNPACK_STAR` | | 250 | `FAST_ATTR_CLEAR` |
| 811 | `UNPACK_FAST` | | 247 | `FAST_BIND` |
| 809 | `FAST_ATTR_STORE` | | 245 | `DEREF` |
| 745 | `IMPORT_FROM` | | 239 | `OBJECT_TYPE` |
| 738 | `COMBINE_KWDS` | | 236 | `POP_FOREACH` |
| 733 | `RAISE` | | 234 | `VAR_REF` |
| 728 | `POP_SHIFT` | | 228 | `POP_TRY` |
| 705 | `ITER_NEXT` | | 226 | `VAR_STORE` |
| 680 | `CALL_FAST_ATTR_NAMES` | | 221 | `FAST` |
| 663 | `FAST_ATTR_INPLACE` | | 218 | `BUILD_ARGS` |
| 629 | `PUSH_FOREACH` | | 218 | `BUILD_KWDS` |
| 595 | `OBJECT_FORMAT` | | 215 | `PUSH_LOOP` |
| 579 | `CALL_VAR_ATTR_NAMES` | | 210 | `IMPORT` |
| 557 | `POP_FINALLY` | | 205 | `AWAIT` |
| 550 | `VAR_ATTR_STORE` | | 205 | `JUMP_FALSE` |
| 546 | `EXCEPTION_MATCH` | | 205 | `JUMP_TRUE` |
| 543 | `SWITCH_EQ_FAST_ATTR` | | 204 | `GLOBALS` |
| 541 | `FAST_ATTR` | | 204 | `POP_LOOP` |
| 540 | `UNPACK_VAR` | | 202 | `POP_COLLECT` |
| 539 | `PROPER` | | 191 | `CAPTURE_CC` |
| 538 | `ITEM_TAKE` | | 191 | `FAST_STORE_CONST` |
| 535 | `POP_PROMPT` | | 183 | `RETURN` |
| 530 | `VAR_ATTR_INPLACE` | | 182 | `CHECK_NIL` |
| 522 | `CALL_FAST_ATTR` | | 178 | `ATTR_CLEAR` |
| 516 | `OBJECT_IS_INSTANCE` | | 178 | `CONST_ITEM_CLEAR` |
| 516 | `OBJECT_IS_SUBCLASS` | | 178 | `PUSH_COLLECT` |
| 513 | `CAR` | | 173 | `BUILD_LIST_APPEND_CONST` |
| 513 | `CDR` | | 173 | `PUSH_CC` |
| 503 | `CALL_FAST_NAMES` | | 169 | `THEEND` |
| 498 | `FAST_LIST_APPEND` | | 167 | `YIELD` |
| 491 | `CALL_ATTR_NAMES` | | 164 | `CALL` |
| 474 | `FAST_ATTR_TAKE` | | 164 | `TAIL_CALL` |
| 474 | `PUSH_SHIFT` | | 162 | `PARAM` |
| 468 | `UNPACK` | | 162 | `VAR` |
| 460 | `SWITCH_EQ_VAR_ATTR` | | 162 | `VAR_ATTR_CLEAR` |
| 450 | `OBJECT_DIVMOD` | | 159 | `BUILD_LIST_CONST` |
| 449 | `POP_DYN_WITH` | | 154 | `CONST` |
| 447 | `CALL_NAMES` | | 154 | `FAST_REF` |
| 445 | `BUILD_LIST_EXTEND_CONST` | | 154 | `PUSH_PARAM` |
| 445 | `ITEM` | | 151 | `VAR_CLEAR` |
| 445 | `VAR_LIST_APPEND` | | 146 | `FAST_TAKE` |
| 442 | `ITEM_REF` | | 141 | `POP` |
| 439 | `SWITCH_EQ_FAST` | | 135 | `PUSH_PROMPT` |
| 429 | `CHECK_EXCEPTION` | | 124 | `VAR_OBJECT_INPLACE` |
| 426 | `CALL_VAR_ATTR` | | 103 | `LOCALS` |
| 420 | `BUILD_LIST_EXTEND` | | 103 | `VAR_STORE_CONST` |
| 411 | `VAR_ATTR` | | 101 | `CLEAR_FOREACH` |
| 410 | `ATTR_TAKE` | | 101 | `CLEAR_LOOP` |
| 402 | `CONST_ITEM_TAKE` | | 88 | `CLEAR_FINALLY` |
| 401 | `ATTR_INPLACE` | | 88 | `CLEAR_TRY` |
| 399 | `CALL_VAR_NAMES` | | 78 | `FAST_OBJECT_INPLACE` |
| 399 | `SWITCH_EQ` | | 76 | `PUSH_FINALLY` |
| 397 | `OBJECT_STR` | | 76 | `PUSH_TRY` |
| 395 | `EXCEPTION` | | 62 | `CHECK_EQ` |
| 394 | `SWITCH_EQ_VAR` | | 62 | `CHECK_GE` |
| 389 | `CALL_FAST` | | 62 | `CHECK_GT` |
| 384 | `CONS` | | 62 | `CHECK_LE` |
| 381 | `OBJECT_EQ` | | 62 | `CHECK_NE` |
| 381 | `OBJECT_GE` | | 62 | `OBJECT_INPLACE` |
| 381 | `OBJECT_GT` | | 62 | `OBJECT_OPERAND` |
| 381 | `OBJECT_LE` | | 60 | `CHECK_IS_INSTANCE` |
| 381 | `OBJECT_LT` | | 60 | `CHECK_IS_SUBCLASS` |
| 381 | `OBJECT_NE` | | 59 | `CHECK_LT` |
| 378 | `FAST_ATTR_REF` | | 59 | `OBJECT_IN` |
| 378 | `VAR_ATTR_TAKE` | | 59 | `OBJECT_IS` |
| 370 | `BREAK` | | 59 | `OBJECT_IS_NOT` |
| 370 | `CONTINUE` | | 59 | `OBJECT_NOT_IN` |
| 353 | `OBJECT_AITER` | | 58 | `CHECK_IS_NOT` |
| 351 | `CALL_ATTR` | | 58 | `CHECK_NOT_IN` |
| 349 | `ATTR_STORE` | | 56 | `DUP` |
| 349 | `CONST_ITEM_STORE` | | 56 | `FAST_CLEAR` |
| 333 | `VAR_BIND` | | 55 | `CHECK_IN` |
| 330 | `BUILD_LIST_APPEND` | | 55 | `CHECK_IS` |
| 329 | `ITEM_CLEAR` | | 52 | `EXCEPTION_CLEAR` |
| 314 | `JUMP_AND` | | 52 | `POP_CC` |
| 314 | `JUMP_OR` | | 52 | `POP_NS` |
| 311 | `VAR_TAKE` | | 52 | `POP_PARAM` |
| 301 | `FAST_STORE` | | 35 | `JUMP` |
| 301 | `TAIL_CALL_NAMES` | | 28 | `LABEL` |
| 296 | `ITEM_STORE` | | 28 | `NOOP` |
| 293 | `BUILD_LIST` | | 28 | `POSITION` |
| 293 | `CALL_VAR` | | 27 | `GLOBALS` |
| 287 | `NILP` | |  |  |

<!-- Regenerate: throwaway build with KOS_OP_INLINE = static KOS_NOINLINE (edit
     koskari/lib/hints.h), rebuild with `uv pip install -qe .`, then
     `nm --print-size --radix=d` on the .so, grep ' t op_[A-Z]'. Dispatch fn
     size and the noinline floor are the `koskari_eval_dispatch_computed_goto`
     symbol in the inlined and noinline builds respectively. Restore hints.h and
     rebuild afterward. See Methodology. -->
