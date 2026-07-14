# Parameterized cold arithmetic opcodes (OPERAND)

Status: Implemented — archived. Consolidation of per-operator arithmetic
opcodes into parameterized cold `OBJECT_OPERAND` / inplace handlers is
complete.

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
-> 71,057 B (var/fast in-place) -> 68,634 B (reducing, step 1) -> 66,749 B
(stack in-place, step 2)**.

**Both remaining families are now migrated** (steps 1 and 2 below, green;
step 1 committed as `aa7836eb`, step 2 green and pending commit):
- The 13 reducing/binary arithmetic `OBJECT_<op>` opcodes collapsed into one
  `OBJECT_OPERAND` (standalone OPCOUNT).
- The 13 stack in-place `OBJECT_INPLACE_<op>` opcodes collapsed into one
  `OBJECT_INPLACE` (standalone OPCOUNT).
`OBJECT_BIT_INVERT` (unary `~`) and `OBJECT_STR` (f-string build, no operand)
stay standalone. Together with the earlier `FAST_OBJECT_INPLACE` /
`VAR_OBJECT_INPLACE` consolidation, the whole arithmetic-opcode surface is now
four parameterized opcodes instead of 52 per-operator ones.

The `.kio` bytecode ABI (`serialize/constants.py`) was bumped MINOR 5 -> 6:
removing 13 + 13 opcode rows shifts the enum values that follow.

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

## Reducing migration -- landed (step 1, green; commit pending)

The actual `OBJECT_<op>` family was more heterogeneous than the first-pass
notes said. Real breakdown (13 operators) and how each is preserved inside the
shared cold `kos_object_operand(es, op, count)` switch in `eval.c`:
- **6 int-fast reducing** `ADD, SUBTRACT, MULTIPLY, BIT_AND, BIT_OR, BIT_XOR`:
  `obj_op_reduce_n_long(es, count, KosOperator_X, KOS_LBOP_X)` (fast path fires
  at `count==2`, else generic reduce).
- **3 non-fast reducing** `FLOOR_DIVIDE, MATRIX_MULTIPLY, TRUE_DIVIDE`:
  `obj_op_reduce_n(es, count, KosOperator_X)` (no `KosLongBinop`, no int-fast).
- **3 strictly-binary** `MODULO, BIT_LEFT, BIT_RIGHT`: `obj_op_binary_n(es,
  PyNumber_X, KOS_LBOP_X)` -- pops exactly 2; count is ignored. (These were
  `NOARG`/`_binary_op` before, never reducing; kept binary.)
- **POWER** `obj_op_power_n(es, count)`: `count==2` -> `pow(b,e)`, `count==3` ->
  `pow(b,e,mod)`. Genuinely not a reduce; the 3-arg modulus form is why POWER
  can't join the reduce cases.

Helper refactor (`eval.c`): the insn-based `obj_op_reducing`/`_reducing_long`/
`_binary_long` became count-explicit cores `obj_op_reduce_n(es,count,fn)`,
`obj_op_reduce_n_long(es,count,fn,lop)`, `obj_op_binary_n(es,fn,lop)`.
`obj_op_reducing(es,insn,fn)` remains as a thin wrapper -- **still used by
OBJECT_STR** (`KosOperator_BuildStr`, not an operand). `obj_op_binary`
(non-long, insn) stays for IN/IS/IS_NOT/NOT_IN. `eval.c` now includes
`operator.h`. `KosOperator_*` reduce fns keep identity (`count==0`) and unary
(`count==1`: `+`->Positive, `-`->Negative) semantics via `reduce_method`.

Compiler (`specials/object.py`): `_reduce_op`/`_binary_op` gained an
`operand=` param; when set they emit `OBJECT_OPERAND (operand, count)` instead
of the per-op opcode. The 10 reducing specials + POWER pass `operand=` (POWER
via `_reduce_op(None, None, ..., operand=POWER)`, count 2 or 3); MODULO/
BIT_LEFT/BIT_RIGHT via `_binary_op(None, ..., operand=X)` (strictly 2).
`normalize_opcount` merges the standalone OPCOUNT unchanged.

**255-count cap (known, accepted):** OPCOUNT's count is one byte, so a reduce
with >255 operands (e.g. a macro-generated `(+ a0 ... a255)`) now raises at
compile time in `merge_opcount` where the old uint16 `COUNT` allowed up to
65535. Deemed acceptable (pathological); revisit only if a real corpus hits it.

Tests: `OBJECT_<op>` opcodes are gone, so bytecode-level tests were updated.
`tests/lib/opcode/__init__.py` `build_code` now **lowers** the readable
per-operator names (`_OPERAND_LOWER` table + `opcount()` helper) to
`OBJECT_OPERAND` -- the same lowering the compiler does -- so the ~100
`["OBJECT_ADD", n]`-style call sites keep expressing the op directly while
exercising the real opcode (no per-test churn). The compile-contract test
`tests/compile/specials/test_object_specials.py` was updated honestly: a
`_find_operand(code, Operand.X)` helper decodes the OPCOUNT count.
`test_expression_function.py` (its own `build_code`) reuses `_lower_instr`.
Native `tests/native/test_eval.c` uses `KosOpCount_MAKE(KOS_OPERAND_ADD, 2)`.

Verification: full suite **4581 passed, 10 skipped**; flake8 OK; churn answers
match (py==ki); ASan **293 passed, 0 failed**; dispatch fn 71,057 -> 68,634 B.

## Stack in-place migration -- landed (step 2, green; commit pending)

`OBJECT_INPLACE_<op>` (13 opcodes, used by `special_inplace_item` /
`special_inplace_attr`, reducing over `count` values) collapsed into one
`OBJECT_INPLACE` (standalone OPCOUNT). This family is uniform -- every operator
maps to its `KosOperator_Inplace*` reducing fn -- so the shared cold
`kos_object_inplace(es, op, count)` switch is 13 `obj_op_reduce_n` calls, with
MODULO keeping its two-operand int-fast path (`obj_op_reduce_n_long`, the only
in-place op that had one) and POWER strictly binary
(`KosOperator_InplacePower` rejects `count != 2`).

Compiler (`specials/object.py`): both `special_inplace_item` and
`special_inplace_attr` emit `OBJECT_INPLACE (operand, 1 + len(values))`;
`_object_inplace_opcode` deleted; the power arity guard now tests `binop is
Operand.KOS_OPERAND_POWER`. `special_inplace` (the var/slot form) still emits
`VAR_OBJECT_INPLACE` -> resolves to `FAST_OBJECT_INPLACE` unchanged.

Tests: the `build_code` lowering table (`_OPERAND_LOWER`) now also maps
`OBJECT_INPLACE_<op>` names -> `OBJECT_INPLACE`, keyed off a shared
`_OPERAND_BY_NAME` dict; `tests/compile/specials/test_inplace_specials.py` got
an honest `_find_inplace(code, Operand.X)` helper for its two opcode
assertions.

Verification: full suite **4581 passed, 10 skipped**; flake8 OK; churn answers
match (py==ki); ASan **293 passed, 0 failed**; dispatch fn 68,634 -> 66,749 B.

### 3. Reducing generalization of the in-place families (optional here)

Make `FAST_OBJECT_INPLACE` / `VAR_OBJECT_INPLACE` fold `count>1` (currently
binary). Requires the compiler fusion to emit count>1 and the shared cores to
loop. This is the "in-place is properly reducing" correctness improvement.

### 4. Delete the unused `.c` files -- DONE (step 4, commit pending)

All 52 dead per-operator handler files removed: 13 `OBJECT_<op>.c`, 13
`OBJECT_INPLACE_<op>.c`, 13 `FAST_OBJECT_INPLACE_<op>.c`, 13
`VAR_OBJECT_INPLACE_<op>.c`. They were `#include`d only via `handlers.incl`
(which lists live opcodes from `opcodes.incl`), and `setup.py` globs
`opcode/*.c` as *depends* only (not as compiled sources -- eval.c is the sole
translation unit and pulls them via `handlers.incl`), so the glob adjusts
automatically. Rebuild + full suite green after removal. `opcode/` now holds
193 files, matching the 193 live opcodes.

### 5. Follow-ups

- `dis.py` / `dis_ng.ki`: decode+render `OPCOUNT` as `<OP> xN`.
- `serialize/codex.py`: confirm OPCOUNT round-trips (plain uint16, like COUNT;
  EXTRA rows already covered) -- add explicit handling if the `arg_kind` wire
  path needs it.
- Tracing (`tracing.c`): per-op timing now buckets a whole family under one
  opcode; sub-bucket by op-type for `*_OPERAND`/`*_INPLACE` to keep
  per-operator visibility (the data this effort steers by). Optional.
- Future: `OBJECT_COMPARE` consolidation of the comparison opcodes.

## Runtime/dispatch layering -- FINALIZED (commit pending)

The consolidation above got the per-operator handler bodies out of the inlined
dispatch loop (120,944 -> ~67k). This pass settles *how the surviving cold math
is written*, so it branches once and runs straight-line. It is size-neutral on
the loop by design (all of it lives in `operator.c`, out of
`koskari_eval_dispatch_computed_goto`); the win is internal quality of the cold
path and the runtime exports.

**The problem it fixes.** Before, the opcode path branched three times: the
computed-goto dispatch, the `switch(op)` in the cold handler, then *again*
inside `kos_long_binop_fast`'s own `switch(op)` -- plus a function-pointer call
through the `KosOperator_*`/`func`/`solofunc` indirection. All of it traced to
one shape: a single shared `reduce_method(op, func, solofunc, ...)` taking the
operator and slots as *runtime arguments*, which kept the inner int-fast switch
live and forced the pointer calls, at both the opcode and runtime origins.

**The fix: monomorphize per operator.** Everything below the single `switch(op)`
now sees the operator and its slot as compile-time constants.

- `operator.c` is the single home of all arithmetic. Layer stack:
  - `kos_as_ll2(l, r, &a, &b)` -- shared *mechanical* extraction of two exact
    ints into `long long` (no operator knowledge, so it never re-branches on
    op).
  - `long_fast_<op>` -- nine hand-split two-operand PyLong fast paths (add,
    subtract, multiply, modulo, bit_and/or/xor, bit_left, bit_right), written
    out explicitly per the "no compiler switch-folding trust; split per op"
    decision. Each returns 1 (handled, `*out` new ref) / 0 (declined) / -1
    (alloc error). Address-taken so the cold tail can call them by pointer.
  - `reduce_slow(name, longfast, func, solofunc, args, count)` -- the cold tail
    (`KOS_NOINLINE`): arity edges 0/1 and the true `count > 2` fold. `longfast`
    is the operator's fast path as a *pointer* (NULL for divide/matmul/power),
    so the fold keeps the int-fast per step. Pointers appear *only here*, in
    cold code.
  - Per-op cores `op_<op>` (13 reducing) and `op_inplace_<op>` (13 in-place),
    all `KOS_OP_INLINE`. Each is self-contained: the `count == 2` hot path
    names its concrete `long_fast_<op>` and its concrete `PyNumber_*` /
    `PyNumber_InPlace*` slot directly -- no switch, no pointer -- and tails to
    `reduce_slow` for every other arity. The in-place cores share the reducing
    `long_fast_<op>` (int OP= int is value-identical to int OP int; only the
    fallback slot differs). Power stays special (2/3-arg; in-place strictly 2).
  - The two dispatchers, `kos_operand_reduce(op, values, count)` and
    `kos_inplace_reduce(op, values, count)` (renamed from `kos_operand_inplace`;
    "inplace" is shorthand for inplace-operand, both reducing). Each is one
    `switch(op)` whose cases *inline* their cores -- "beefy-ish" but the only
    copy of the switch in the binary, and once entered it runs straight-line
    with no per-op call. Declared in `operator.h`, so the eval TU calls across
    the boundary.
  - Runtime exports: the 9 module-exposed `KosOperator_*` (`+ - * & | ^ // / @`)
    are thin wrappers over their cores, so first-class operator values stay
    zero-switch. The dispatch-only operators (`% << >> **` and every in-place)
    shed their `KosOperator_*` exports -- they are reached only through the
    dispatchers -- and their 17 declarations left `operator.h`.
- `eval/eval_obj.c` keeps the hot addressing helpers (`kos_object_operand`,
  `kos_object_inplace`, `kos_fast_inplace_operand`, `kos_var_inplace_operand`,
  `kos_attr_inplace_reduce`): resolve origin/destination, call one dispatcher,
  store. The dispatchers themselves moved to `operator.c`.

**Branch accounting after.** Opcode path: dispatch-goto + one `switch(op)` +
straight-line arithmetic. Runtime path (`KosOperator_Add` from `map`, etc.):
zero switches. The `kos_long_binop_fast` re-switch and all hot-path function
pointers are gone.

**Storage families covered** (all funnel into the two dispatchers): reducing
`OBJECT_OPERAND`; in-place `OBJECT_INPLACE`, `FAST_OBJECT_INPLACE`,
`VAR_OBJECT_INPLACE`, `FAST_ATTR_INPLACE`, `VAR_ATTR_INPLACE`.

Verification: full suite **4581 passed, 10 skipped**; churn answers match
(py==ki); ASan **293 passed, 0 failed**; clean `-Werror` build; dispatch fn
67,274 B (loop unaffected, as expected).

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
