# Dispatch function sizing baseline

Internal analysis for the effort to shrink `koskari_eval_dispatch_computed_goto`,
the single computed-goto dispatch function into which every inline opcode
handler is `always_inline`d (`KOS_OP_HANDLER`); handlers marked
`KOS_OP_HANDLER_COLD` are `noinline` and called from their dispatch label
instead.  As the opcode set grows, each inline handler's compiled size is
additive I-cache cost in that one function.  Goal: keep the hottest short paths
inline, migrate everything else one or two calls out.

## Methodology

Per-handler sizes come from a throwaway measurement build with `KOS_OP_INLINE`
redefined to `static KOS_NOINLINE`, so each `op_<NAME>` becomes its own symbol;
sizes read via `nm --print-size` on the built extension.  Standalone noinline
size is a proxy for a handler's inlined contribution (inlining can specialise or
shrink it) -- good enough to rank and prioritise.  The proxy is close but
inexact: the dispatch fn (58,433 B) sits within ~120 B of floor + inline
bodies (58,312 B) and far below floor + all bodies (101,996 B); at
`-O2 -flto` inlining shares tails and specialises roughly as much as the
per-op call glue costs.  Hotness is reasoned, not profiled: there is
no PGO/instrumented build yet (deferred until we have representative profiling
apps -- scheduler, koslife, churns, mcp, repl, dis).

Measured 2026-07-18 (post boundary dispatcher + phase E decompose), 186
opcodes (one handler each; 187 symbols counting the split-out
`GLOBALS.cold`).  67 handlers are `KOS_OP_HANDLER_COLD`: their bodies
(43,684 B standalone) live out of line and the dispatch label calls them;
the remaining 119 inline handlers contribute 35,672 B of standalone-proxy
body to the dispatch fn.

## Changes since the phase A2 measurement (same day)

Two efforts landed after the A2 sweep:

- **Generic iterator-boundary dispatcher** (§5.2 of
  [interpreter/boundary-control-reemit.md](interpreter/boundary-control-reemit.md)):
  NEXT / FOREACH / YIELD_FROM boundary routing converged on the
  capability-keyed dispatcher in `dynchain/dyn_boundary.c`; the three
  per-kind finders and the probe-then-refind double walk are gone.
  Dispatch fn +24 B (inline capability toggle in ITER_NEXT).
- **Phase E decompose**: YIELD_FROM's four `drive_*` variants now share
  noinline helpers for the yield-out / exhaustion-pop /
  StopAsyncIteration-harvest / await-resume motifs, and delegate
  classification is one function used by both normalize paths.
  `op_YIELD_FROM` 6,728 -> 5,346 B (-20.5%), plus ~1,140 B of shared
  helpers out of line (net cold-family footprint 6,489 B, with the
  duplication gone).  ITER_NEXT split hot/cold: the sync value path
  stays inline (standalone body 705 -> 615 B) and the Koskari-agen
  attach branch became a 190 B noinline helper.

Net: total handler bodies 80,844 -> 79,356 B; dispatch fn
58,374 -> 58,433 B (+59 B, call glue and layout noise -- these changes
target cold `.text` and dedup, not the loop).

## Changes since the 2026-07-17 measurement

**Phase A2 cold sweep** (this measurement): nine more reasoned-cold
handlers went `KOS_OP_HANDLER_COLD` -- the three phase A stragglers
(`BUILD_CLASS` 1,106 B, `BUILD_FUNCTION` 887 B, `UNPACK_FAST` 811 B,
named in the phase A list but never converted) plus `PUSH_WITH` (848 B),
`PUSH_DYN_WITH` (832 B), `COMBINE_KWDS` (738 B), `RAISE` (733 B),
`POP_FINALLY` (557 B), and `OBJECT_DIVMOD` (450 B).  All run at most
once per block entry/exit, definition, splat call, or exceptional path;
none sit in loop bodies.  Dispatch fn 65,435 -> 58,374 B (-10.8%),
now 57.0 KiB -- under the 64 KiB L1-instruction line with ~7 KiB of
headroom (previously it straddled it at 63.9 KiB).

Also a records correction: **phase A itself landed 2026-07-12**
(commit `13835872`, 24 handlers including `YIELD_FROM` and `NEXT`)
but was never marked DONE here; every measurement from 2026-07-15
onward already excluded those bodies from the dispatch fn.

## Changes since the 2026-07-15 measurement

Three efforts landed between measurements:

- **Comparison unification** (see
  [../archive/compare-unification.md](../archive/compare-unification.md)):
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

Net: the fully-inlined dispatch fn dropped 70,962 -> 65,435 B (-7.8%),
while gaining eight new opcodes.  Cumulative from the 2026-07-12
baseline: 120,944 -> 65,435 B (-45.9%).

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
dispatch fn 81,418 -> 70,962 B).  The comparison family has now followed the
same selector-keyed consolidation pattern.

## Baseline

| quantity | bytes | meaning |
|---|---:|---|
| dispatch fn | 58,433 | current `koskari_eval_dispatch_computed_goto` (+30 B `.cold`); 57.1 KiB |
| floor (all 186 handlers un-inlined) | 22,640 | per-op glue (label, NORMAL-check, gil-poll, prepare-next, `goto`, one call) x 186 + loop scaffold |
| handler bodies | 79,356 | sum of standalone noinline bodies -- the carving budget |
| cold bodies (out of line) | 43,684 | 67 `KOS_OP_HANDLER_COLD` handlers, excluded from the dispatch fn |
| inline bodies | 35,672 | 119 inline handlers -- the remaining in-loop budget |

Distribution (top-heavy, long-tailed; % of the 79,356 B body):

- `>= 1000 B`: 13 handlers, 20,809 B (26% of body)
- `>=  500 B`: 47 handlers, 43,010 B (54% of body)
- `>=  200 B`: 140 handlers, 73,884 B (93% of body)
- `>=  100 B`: 169 handlers, 78,387 B (99% of body)

## Clusters (size x reasoned hotness x structure)

| cluster | bytes (handlers) | % body | hotness | action |
|---|---:|---:|---|---|
| `CALL_*` family (incl. `TAIL_CALL*`) | 14,577 (21) | 18.4% | mixed | DONE (call-unification): prologues + shared core; residual is the 7 STAR gathers (8,868 B), kept by design |
| YIELD_FROM | 5,346 (1) | 6.7% | cold | DONE (phase A un-inline + phase E decompose): drive family shares noinline motif helpers; the remaining direction is the GEN yield-from passthru idea (undecided, see [TODO.md](TODO.md)) |
| comparison family (`*_COMPARE*`) | 2,642 (6) | 3.3% | hot | DONE (compare-unification): selector-keyed, fused pairs born inline, VAR twins cold |
| IMPORT_* | 2,016 (3) | 2.5% | cold | DONE (phase A): `IMPORT_STAR`/`IMPORT_FROM` cold; `IMPORT` (210 B) stays inline |
| bulk loads (`VAR2/4`, `FAST2/4`) | 1,738 (4) | 2.2% | hot | DONE (var4): FAST pair inline, VAR twins cold |
| `*_ATTR_INPLACE` | 1,594 (3) | 2.0% | warm | already op-parameterised; all three now cold (phase A + VAR sweep) |
| `*_OBJECT_*` arith/operand | 326 (4) | 0.4% | warm | already consolidated (Phase B) -- keep inline |

Largest handlers still inline (all reasoned hot or warm):
`PUSH_NS` (842 B, let/block entry),
`FAST_ATTR_STORE` (809 B, cached attr write), `CHECK_COMPARE_FAST`
(679 B, loop bounds), `PUSH_FOREACH` (629 B, loop entry), `ITER_NEXT`
(615 B, loop step -- hot/cold split done, sync value path only),
`OBJECT_FORMAT` (595 B), `FAST2` (569 B), `EXCEPTION_MATCH` (546 B,
except-clause path -- borderline; EAFP-heavy code keeps it warm).

## Planned phases

- **A -- un-inline cold giants**: DONE (2026-07-12, commit `13835872`).
  24 handlers marked `KOS_OP_HANDLER_COLD`: YIELD_FROM, NEXT, IMPORT_STAR/FROM,
  CALL_*_STAR/NAMES, TAIL_CALL_STAR/NAMES, PUSH_WIND, POP_PROMPT/SHIFT,
  PAIR_SPLICE, UNPACK_STAR, COMBINE_ARGS.  Dispatch fn 120,944 -> 94,511 B.
  Three handlers on the original list (BUILD_CLASS, BUILD_FUNCTION,
  UNPACK_FAST) were left inline; picked up by A2.
- **A2 -- second cold sweep**: DONE (2026-07-18).  The three phase A
  stragglers plus PUSH_WITH, PUSH_DYN_WITH, COMBINE_KWDS, RAISE,
  POP_FINALLY, OBJECT_DIVMOD.  Dispatch fn 65,435 -> 58,374 B.
- **B -- share arithmetic families**: DONE.  Landed as opcode consolidation
  (`OBJECT_OPERAND`/`OBJECT_INPLACE`/`FAST_OBJECT_INPLACE`/`VAR_OBJECT_INPLACE`
  and the `*_ATTR_INPLACE` trio) rather than as slim inline stubs over a shared
  helper.
- **C -- decompose CALL_FAST_ATTR / CALL_FAST**: DONE, subsumed by the
  call-unification prologue/core split (kos_call_invoke); see
  [interpreter/call-unification.md](interpreter/call-unification.md).
- **D -- selector-keyed comparisons**: DONE, landed as compare-unification;
  see [../archive/compare-unification.md](../archive/compare-unification.md).
- **E -- decompose the cold giants' bodies**: DONE (2026-07-18).
  YIELD_FROM's four `drive_*` variants now share noinline helpers for
  the yield-out / exhaustion-pop / StopAsyncIteration-harvest /
  await-resume motifs (`op_YIELD_FROM` 6,728 -> 5,346 B), and ITER_NEXT
  split hot/cold (705 -> 615 B inline, agen attach out of line).  As
  predicted these shrank total `.text` and cold-path I-cache, not the
  dispatch fn.  The remaining yield-from direction is the GEN passthru
  idea (undecided; see [TODO.md](TODO.md)).

Remeasure dispatch fn size and run the suite + ASan after each batch.

## Full per-handler sizes (bytes, descending)

Handlers marked with a dagger are `KOS_OP_HANDLER_COLD`: their bodies
live out of line and do not contribute to the dispatch fn.

| bytes | handler | | bytes | handler |
|---:|---|---|---:|---|
| 5,346 | `YIELD_FROM` † | | 314 | `JUMP_OR` |
| 1,845 | `NEXT` † | | 314 | `JUMP_AND` |
| 1,712 | `CALL_ATTR_STAR` † | | 311 | `VAR_TAKE` † |
| 1,493 | `CALL_VAR_ATTR_STAR` † | | 301 | `TAIL_CALL_NAMES` † |
| 1,417 | `PUSH_WIND` † | | 301 | `FAST_STORE` |
| 1,301 | `CALL_FAST_ATTR_STAR` † | | 296 | `ITEM_STORE` |
| 1,210 | `TAIL_CALL_STAR` † | | 293 | `CALL_VAR` † |
| 1,210 | `CALL_STAR` † | | 293 | `BUILD_LIST` |
| 1,106 | `BUILD_CLASS` † | | 287 | `OBJECT_COMPARE_VAR` † |
| 1,061 | `IMPORT_STAR` † | | 287 | `NILP` |
| 1,051 | `PAIR_SPLICE` † | | 285 | `OBJECT_VARS` |
| 1,043 | `COMBINE_ARGS` † | | 279 | `SWITCH_EQ_CONST` |
| 1,014 | `CALL_FAST_STAR` † | | 277 | `BUILD_KWDS_NAMES` |
| 928 | `CALL_VAR_STAR` † | | 275 | `OBJECT_FORMAT_CONST` |
| 887 | `BUILD_FUNCTION` † | | 275 | `CONST_ITEM_REF` |
| 848 | `PUSH_WITH` † | | 275 | `CONST_ITEM` |
| 842 | `PUSH_NS` | | 275 | `ATTR_REF` |
| 832 | `PUSH_DYN_WITH` † | | 275 | `ATTR` |
| 821 | `UNPACK_STAR` † | | 274 | `OBJECT_TRUTH` |
| 811 | `UNPACK_FAST` † | | 274 | `OBJECT_NOT` |
| 809 | `FAST_ATTR_STORE` | | 273 | `POP_WIND` |
| 745 | `IMPORT_FROM` † | | 267 | `OBJECT_LEN` |
| 738 | `COMBINE_KWDS` † | | 261 | `POP_WITH` |
| 733 | `RAISE` † | | 259 | `VAR_ATTR_REF` † |
| 728 | `POP_SHIFT` † | | 253 | `OBJECT_REPR` |
| 680 | `CALL_FAST_ATTR_NAMES` † | | 253 | `OBJECT_ITER` |
| 679 | `CHECK_COMPARE_FAST` | | 253 | `OBJECT_DIR` |
| 663 | `FAST_ATTR_INPLACE` † | | 253 | `OBJECT_BIT_INVERT` |
| 629 | `PUSH_FOREACH` | | 250 | `FAST_ATTR_CLEAR` |
| 615 | `ITER_NEXT` | | 247 | `FAST_BIND` |
| 595 | `OBJECT_FORMAT` | | 245 | `DEREF` |
| 579 | `CALL_VAR_ATTR_NAMES` † | | 239 | `OBJECT_TYPE` |
| 569 | `FAST2` | | 236 | `POP_FOREACH` |
| 557 | `POP_FINALLY` † | | 234 | `VAR_REF` † |
| 550 | `VAR_ATTR_STORE` † | | 231 | `GLOBALS` |
| 546 | `EXCEPTION_MATCH` | | 228 | `POP_TRY` |
| 543 | `SWITCH_EQ_FAST_ATTR` | | 226 | `VAR_STORE` † |
| 541 | `FAST_ATTR` | | 221 | `FAST` |
| 540 | `UNPACK_VAR` † | | 218 | `BUILD_KWDS` |
| 539 | `PROPER` | | 218 | `BUILD_ARGS` |
| 538 | `ITEM_TAKE` | | 215 | `PUSH_LOOP` |
| 535 | `POP_PROMPT` † | | 210 | `IMPORT` |
| 530 | `VAR_ATTR_INPLACE` † | | 205 | `JUMP_TRUE` |
| 522 | `CALL_FAST_ATTR` | | 205 | `JUMP_FALSE` |
| 513 | `CDR` | | 205 | `AWAIT` |
| 513 | `CAR` | | 204 | `POP_LOOP` |
| 503 | `CALL_FAST_NAMES` † | | 202 | `POP_COLLECT` |
| 498 | `FAST_LIST_APPEND` | | 191 | `FAST_STORE_CONST` |
| 491 | `CALL_ATTR_NAMES` † | | 191 | `CAPTURE_CC` |
| 474 | `PUSH_SHIFT` | | 183 | `RETURN` |
| 474 | `FAST_ATTR_TAKE` | | 182 | `CHECK_NIL` |
| 468 | `UNPACK` | | 178 | `PUSH_COLLECT` |
| 467 | `CHECK_COMPARE_VAR` † | | 178 | `CONST_ITEM_CLEAR` |
| 460 | `SWITCH_EQ_VAR_ATTR` † | | 178 | `ATTR_CLEAR` |
| 450 | `OBJECT_DIVMOD` † | | 173 | `PUSH_CC` |
| 449 | `POP_DYN_WITH` | | 173 | `BUILD_LIST_APPEND_CONST` |
| 447 | `CALL_NAMES` † | | 169 | `THEEND` † |
| 445 | `VAR_LIST_APPEND` † | | 167 | `YIELD` |
| 445 | `ITEM` | | 164 | `TAIL_CALL` |
| 445 | `BUILD_LIST_EXTEND_CONST` | | 164 | `CALL` |
| 442 | `ITEM_REF` | | 162 | `VAR_ATTR_CLEAR` † |
| 439 | `SWITCH_EQ_FAST` | | 162 | `VAR` † |
| 437 | `CHECK_COMPARE` | | 162 | `PARAM` |
| 429 | `CHECK_EXCEPTION` | | 159 | `BUILD_LIST_CONST` |
| 426 | `CALL_VAR_ATTR` † | | 154 | `PUSH_PARAM` |
| 423 | `FAST4` | | 154 | `FAST_REF` |
| 420 | `BUILD_LIST_EXTEND` | | 154 | `CONST` |
| 411 | `VAR_ATTR` † | | 151 | `VAR_CLEAR` † |
| 410 | `OBJECT_COMPARE` | | 146 | `FAST_TAKE` |
| 410 | `ATTR_TAKE` | | 141 | `POP` |
| 402 | `CONST_ITEM_TAKE` | | 135 | `PUSH_PROMPT` |
| 401 | `ATTR_INPLACE` † | | 124 | `VAR_OBJECT_INPLACE` † |
| 399 | `SWITCH_EQ` | | 103 | `VAR_STORE_CONST` † |
| 399 | `CALL_VAR_NAMES` † | | 103 | `LOCALS` |
| 395 | `EXCEPTION` | | 101 | `CLEAR_LOOP` |
| 394 | `SWITCH_EQ_VAR` † | | 101 | `CLEAR_FOREACH` |
| 389 | `CALL_FAST` | | 88 | `CLEAR_TRY` |
| 384 | `CONS` | | 88 | `CLEAR_FINALLY` |
| 381 | `OBJECT_STR` | | 78 | `FAST_OBJECT_INPLACE` † |
| 378 | `VAR_ATTR_TAKE` † | | 76 | `PUSH_TRY` |
| 378 | `FAST_ATTR_REF` | | 76 | `PUSH_FINALLY` |
| 375 | `VAR4` † | | 62 | `OBJECT_OPERAND` † |
| 371 | `VAR2` † | | 62 | `OBJECT_INPLACE` † |
| 370 | `CONTINUE` | | 56 | `FAST_CLEAR` |
| 370 | `BREAK` | | 56 | `DUP` |
| 362 | `OBJECT_COMPARE_FAST` | | 52 | `POP_PARAM` |
| 353 | `OBJECT_AITER` | | 52 | `POP_NS` |
| 351 | `CALL_ATTR` † | | 52 | `POP_CC` |
| 349 | `CONST_ITEM_STORE` | | 52 | `EXCEPTION_CLEAR` |
| 349 | `ATTR_STORE` | | 35 | `JUMP` |
| 333 | `VAR_BIND` † | | 28 | `POSITION` † |
| 330 | `BUILD_LIST_APPEND` | | 28 | `NOOP` † |
| 329 | `ITEM_CLEAR` | | 28 | `LABEL` † |

<!-- Regenerate: throwaway build with KOS_OP_INLINE = static KOS_NOINLINE (edit
     koskari/lib/hints.h), rebuild with `uv pip install -qe .`, then
     `nm --print-size --radix=d` on the .so, grep ' t op_[A-Z]' (fold any
     op_<NAME>.cold split symbol into its handler). Dispatch fn size and the
     noinline floor are the `koskari_eval_dispatch_computed_goto` symbol in the
     inlined and noinline builds respectively. Cold markers (†) come from
     `grep -l '^KOS_OP_HANDLER_COLD' koskari/lib/opcode/*.c`. Restore hints.h
     and rebuild afterward. See Methodology. -->
