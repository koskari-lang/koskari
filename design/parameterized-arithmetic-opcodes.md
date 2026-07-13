# Parameterized cold arithmetic opcodes (OPERAND)

Status: in progress on branch `eval_fast_deref`. This doc is the resume point
-- read it top to bottom to pick the work back up.

## Goal

Collapse the arithmetic opcode families (per-operator opcodes) into a few
parameterized **cold** opcodes carrying an operator selector, dispatched once
by the computed goto and then switching on the operator *inside* a cold,
out-of-line handler (warm inside, cold outside). This shrinks the single
`always_inline` dispatch function `koskari_eval_dispatch_computed_goto` (both
its body and the per-opcode floor) and, more importantly, stops opcode
proliferation as ~39 more opcodes arrive. Cachegrind established un-inlining
big handlers is ~cycle-neutral (I-cache offsets the call), and the op-type
switch is a *win* on `churn_loop` under a pessimistic branch model (fewer
total mispredicts + better locality; see the gate result below).

## Naming convention (settled)

- **OPERAND** -- the parameterized reducing operator family. `OBJECT_ADD` ->
  `OBJECT_OPERAND (op, count)`.
- **INPLACE** -- stands in for the long `INPLACE_OPERAND`. `OBJECT_INPLACE_ADD`
  -> `OBJECT_INPLACE (op, count)`; likewise `FAST_OBJECT_INPLACE`,
  `VAR_OBJECT_INPLACE`.
- **COMPARE** -- reserved for a future consolidation of the comparison
  opcodes (`OBJECT_EQ`/`LT`/... -> `OBJECT_COMPARE`), same pattern.
- The C op-type enum is `KosOperand` / `KOS_OPERAND_*` (Python `Operand`),
  generated from `KOS_OPERAND_LIST` in `koskari/lib/operands.h`.
- The pre-existing long-integer int-fast path is a *separate* concept and
  keeps its own names: `KosLongBinop` / `kos_long_binop_fast` / `KOS_LBOP_*`.
- `KOSTYLE_OPCOUNT` keeps its name -- it packs op-type + count regardless.

## The one new argument type: `KOSTYLE_OPCOUNT`

`KosOp` is `uint16 op + uint16 arg`. `OPCOUNT` fuses an operator selector and a
reduce count into one 16-bit `arg`:

```
arg = (op_type << 8) | count      op_type high byte (KosOperand 0..12)
                                  count   low byte  (reduce arity)
```

`bytecode.h`: `KosOpCount_MAKE(op,count)`, `KosOpCount_TYPE(v)`,
`KosOpCount_COUNT(v)`. Used **directly** as the whole arg where op-type+count
is all that travels (reducing `OBJECT_OPERAND` / `OBJECT_INPLACE`), or as one
field of an **EXTRA** row when a fast-slot or var-name also travels
(`FAST_OBJECT_INPLACE` = `EXTRA[OPCOUNT, FASTVAR]`, `VAR_OBJECT_INPLACE` =
`EXTRA[OPCOUNT, VAR]`). Indices are never byte-packed; VAR/FASTVAR keep full
width in their own EXTRA slots.

## Status -- DONE (committed on eval_fast_deref)

Dispatch fn `koskari_eval_dispatch_computed_goto`: **124,261 B at effort start
-> 71,057 B now (-43%)**.

Commit trail (oldest -> newest):
- `f58796f7` slim ns-touching handlers (pre-work; 124,261 -> 120,944)
- `e3752e41` design: sizing baseline (`design/dispatch-sizing.md`)
- `13835872` un-inline cold-giant handlers (Phase A; 120,944 -> 94,511)
- `9c62b4cc`,`80a7ea12` design: this plan (+ single-OPCOUNT correction)
- `97b2f9f7` **gate**: shared reduce handler + `operands.h` (`KOS_OPERAND_LIST`,
  `KosOperand`); 13 `FAST_OBJECT_INPLACE_<op>` became stubs. Gate result below.
- `ee427be1` `KOSTYLE_OPCOUNT` + `KosOpCount_*` + `Operand` enum exposed to
  Python; `FAST_OBJECT_INPLACE` opcode (EXTRA) + cold handler (dead at first)
- `f15cc87b` `merge_opcount` compile helper
- `825476fe` **collapse live**: `special_inplace` emits `VAR_OBJECT_INPLACE`
  `EXTRA[(op,1), name]`; resolution lowers to `FAST_OBJECT_INPLACE`; both cold
  handlers via shared cores; C EXTRA-row raw-OPCOUNT fix; `normalize_opcount`
  pass
- `f70d53bc` removed the 26 per-operator `FAST/VAR_OBJECT_INPLACE_<op>` opcode
  rows (`.c` files left on disk); `object.py` tables become direct symbol->
  `Operand`; 79,162 -> 71,057
- `d055246d` rename BINOP -> OPERAND

Every commit: full suite green (4581 passed, 10 skipped) + churn answers match.

**Gate result** (cachegrind `churn_loop`, cache+branch sim, shared-switch vs
plain `_COLD` un-inline): switch build ~0.7% *faster* -- total mispredicts
140.2M vs 143.1M, I1 misses 43.5M vs 51.3M (one shared hot handler beats 13
scattered), under cachegrind's pessimistic branch model. The op-type switch is
not a cost.

So the **in-place families are fully migrated and live**: `FAST_OBJECT_INPLACE`
and `VAR_OBJECT_INPLACE` are the only in-place opcodes; 26 -> 2.

## Hot/cold architecture + the X-macro (implemented, reused for reducing)

- Handlers are `KOS_OP_HANDLER_COLD` (`static KOS_NOINLINE`) -- one tiny call
  stub per opcode in the dispatch loop; the switch + operators live out of
  line.
- The operator set is one source of truth: `KOS_OPERAND_LIST(X)` in
  `operands.h`, `X(NAME, int-fast KosLongBinop or KOS_LBOP_NONE, reduce-fn,
  in-place-fn)`. It generates the `KosOperand` enum and every family's switch.
  `KOS_LBOP_NONE` (added to `longops.h`) marks operators with no int-fast path;
  `kos_long_binop_fast` already returns 0 (its `default`) for it. Power's
  ternary is wrapped by `kos_operand_power` / `kos_operand_inplace_power`
  shims in `operands.h`.
- Shared cores today (both `KOS_NOINLINE`, runtime op-type switch = real
  polymorphic branch): `kos_fast_inplace_operand(es, op, idx)` in
  `eval/eval_utils.c`; `kos_var_inplace_operand(es, op, name)` in `eval.c`
  (after `frame_ns_deref`/`frame_ns_store`, which it needs). Both binary
  (count 1) for now -- reducing generalization (count>1) is a later fold.
- No re-branch/helper-chain: each switch case passes a *constant* `KOS_LBOP_*`,
  so the int-fast path stays specialized per case.

## Implementation notes & gotchas (hard-won -- read before touching)

- **`convert_fast` is VAR->FAST-only and OPTIONAL.** Never munge arg types in
  it. Operand normalization must run unconditionally *earlier*:
  `Compiler.normalize_opcount(instr)` runs in `complete()` right after
  `compress`, before the optional `convert_fast`. It merges any
  `KOSTYLE_OPCOUNT` operand (standalone) or OPCOUNT field of an EXTRA row via
  `merge_opcount`.
- **`merge_opcount(value)`** (`compiler.py`) accepts a `(op_type, count)` pair
  (merged) or a pre-merged int (validated); range-checks both bytes and
  requires a known `Operand`. Specials emit the readable `(op, count)` pair;
  no soften-side reversal.
- **EXTRA arg tuples are 4-tuples** matching the schema, unused fields `0`,
  e.g. `((op, 1), name, 0, 0)`. Field order: schema field i -> row `a{i}`
  (confirmed via `FAST_ATTR`: `a0`=field0). Handlers read `row.a0`=OPCOUNT,
  `row.a1`=slot/name; name via `KosEvalState_CONST(es, row.a1)`.
- **The resolution pass is already EXTRA-aware** (`compiler.py` ~1044-1065):
  it zips arg with `op.extra` and lowers each `KOSTYLE_VAR` field to a fast
  slot, swapping VAR->FAST opcode when all resolve; non-VAR fields (incl. a
  merged OPCOUNT) pass through. `VAR_TO_FAST` is rebuilt from FAST_* opcode
  names, so `FAST_OBJECT_INPLACE` <-> `VAR_OBJECT_INPLACE` maps automatically.
- **C harden of OPCOUNT** (two paths):
  - EXTRA field: `bytecode.c` ~1586 -- `KOSTYLE_OPCOUNT` added to the raw-int
    branch (`COUNT || FASTVAR || STAR || OPCOUNT`). This was a real bug: without
    it the op-type was silently zeroed (`a0`=0), giving wrong results.
  - Standalone: `bytecode.c` ~1883 catch-all `else { COUNT, NOARG, etc: raw }`
    already covers OPCOUNT (arg is a PyLong after `normalize_opcount`).
    **Confirmed sufficient for `OBJECT_OPERAND`.**
- **`Operand` enum** is generated in `bytecode.c` `create_operand_enum()` from
  `KOS_OPERAND_LIST`, registered as module attr `"Operand"`, member names
  `KOS_OPERAND_ADD` etc. (`int_enum` stringifies). `object.py` maps operator
  symbol -> `Operand` directly (`_OPERAND_BY_SYM` + plain/`compile/` forms ->
  `_INPLACE_OPERAND_MAP`); `_object_inplace_opcode(operand)` -> the
  `OBJECT_INPLACE_<op>` opcode (used by `special_inplace_item` today).
- **`dis` cosmetic**: OPCOUNT fields render as their raw int / `0`; the decoder
  doesn't unpack `(op, count)` yet. Runtime is correct. Follow-up.
- Adding/removing opcodes shifts enum values and requires `tox -qe handlers`
  to regenerate `handlers.incl`. Bootstrap recompiles, so no stale bytecode.

## Remaining work

### 1. Reducing migration -> `OBJECT_OPERAND` (correctness-sensitive)

The reducing `OBJECT_<op>` family is heterogeneous -- preserve semantics
exactly:
- 9 ops `ADD, SUBTRACT, MULTIPLY, MODULO, BIT_AND/OR/XOR/LEFT/RIGHT`:
  `OP(..., 0, COUNT)`, handler `obj_op_reducing_long(es, insn, KosOperator_X,
  KOS_LBOP_X)` (int-fast when `ARG==2`, else `obj_op_reducing`).
- 3 ops `FLOOR_DIVIDE, MATRIX_MULTIPLY, POWER`: `COUNT`, handler
  `obj_op_reducing(es, insn, KosOperator_X)` (no int-fast).
- `TRUE_DIVIDE`: `NOARG` style, emitted binary via `_binary_op` (`opa`), handler
  `obj_op_reducing(...)`. (Check the exact emit arg.)
- All fold through hardened `KosOperator_*` (`KosOperatorReducing *`) with
  identity (`count==0` -> `fn(NULL,0)`) and single-arg semantics in
  `obj_op_reducing` (eval.c). `obj_op_reducing` reads `count = KosOp_ARG(insn)`.

Plan:
- Add `OP(OBJECT_OPERAND, 0, OPCOUNT)` + cold handler: `op =
  KosOpCount_TYPE(arg)`, `count = KosOpCount_COUNT(arg)`, call a shared
  `kos_object_operand(es, op, count)` that `switch`es on op and, per case,
  reduces `count` operands with a *constant* `lop` + the op's `KosOperator_*`
  fn. Retires `obj_op_reducing_long`'s runtime `lop` switch.
- Refactor `obj_op_reducing` to a `kos_object_reduce_n(es, count, fn)` taking
  count explicitly (the current one reads `insn->ARG`); the fast-int `count==2`
  shortcut per case uses the constant `lop`. Preserve identity/single-arg
  behavior verbatim.
- Compiler: `_reduce_op` (and the binary `_binary_op` path for `/`, `%`, `**`,
  etc.) in `koskari/compile/specials/object.py` emit
  `OBJECT_OPERAND (operand, count)` instead of the per-op opcodes.
  `normalize_opcount` already merges standalone OPCOUNT.
- Remove the 13 `OBJECT_<op>` rows from `opcodes.incl`.

### 2. Stack in-place -> `OBJECT_INPLACE`

`OBJECT_INPLACE_<op>` (used by `special_inplace_item`, already reducing over
`count` values). Add `OP(OBJECT_INPLACE, 0, OPCOUNT)` + cold handler (same
switch shape, TOS accumulator epilogue). Update `special_inplace_item` to emit
`(operand, count)`; drop `_object_inplace_opcode`. Remove the 13
`OBJECT_INPLACE_<op>` rows.

### 3. Reducing generalization of the in-place families (optional here)

Make `FAST_OBJECT_INPLACE` / `VAR_OBJECT_INPLACE` fold `count>1` (currently
binary). Requires the compiler fusion to emit count>1 and the shared cores to
loop. This is the "in-place is properly reducing" correctness improvement.

### 4. Delete the unused `.c` files

After 1-2, the per-operator handler files left on disk are all dead:
`koskari/lib/opcode/{FAST,VAR}_OBJECT_INPLACE_<op>.c` (26, already dead) and
`OBJECT_<op>.c` / `OBJECT_INPLACE_<op>.c` (after their migration). Delete them.
`handlers.incl` already excludes them (only lists opcodes in `opcodes.incl`).

### 5. Follow-ups

- `dis.py` / `dis_ng.ki`: decode+render `OPCOUNT` as `<OP> xN`.
- `serialize/codex.py`: confirm OPCOUNT round-trips (plain uint16, like COUNT;
  EXTRA rows already covered) -- add explicit handling if the `arg_kind` wire
  path needs it.
- Tracing (`tracing.c`): per-op timing now buckets a whole family under one
  opcode; sub-bucket by op-type for `*_OPERAND`/`*_INPLACE` to keep
  per-operator visibility (the data this effort steers by). Optional.
- Future: `OBJECT_COMPARE` consolidation of the comparison opcodes.

## Verification per step

Rebuild (`tox -qe churn` reinstalls + builds; direct `.tox/churn/bin/python`
uses the *installed* copy, which can be stale for `.py` edits -- run through
`tox` after Python changes). Gate each step on: churn answers match (the
harness asserts py==ki), full suite (`tox -qe test`, expect 4581), dispatch fn
size via `nm --print-size` on the `.so` (symbol
`koskari_eval_dispatch_computed_goto.lto_priv.0`), and cachegrind
(`--cache-sim=yes --branch-sim=yes`) on `churn_loop` for the reducing switch.
Native memory: `make test-asan` (container) for refcount-touching changes.

<!-- Companion: design/dispatch-sizing.md (sizing baseline + un-inline-by-size rule). -->
