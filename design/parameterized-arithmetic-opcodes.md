# Parameterized cold arithmetic opcodes (double-branch)

Status: proposal. Branch context: `eval_fast_deref` (dispatch-size effort).

## Motivation

Every opcode handler is `always_inline`d into the single computed-goto
`koskari_eval_dispatch_computed_goto`, so each handler's body is additive
I-cache cost in one hot function, and each opcode is fixed dispatch glue
(~120 B/op of the 30,929 B floor). The arithmetic families are the worst
offenders: ~52 near-identical handlers across four families, ~26% of the
handler-body budget, and they multiply the opcode count. With ~39 more
opcodes queued, the *count* is the structural problem, not just the bytes.

Cachegrind established the enabling fact: un-inlining a big handler is
~cycle-neutral -- the ~617K L1i misses it saves (tighter dispatch loop)
offset the ~5M added instructions (the call). So moving arithmetic out of
line costs ~nothing in cycles. The remaining question is whether we also
collapse the *opcodes*: fold each family's per-operator opcodes into one
parameterized opcode carrying an op-type, dispatched cold, with the
per-operator work inlined *inside* that cold handler.

This is a deliberate, scoped exception to Koskari's early-fork idiom (one
distinct opcode per variation, selection resolved by the computed goto). We
keep early-fork for hot standalone opcodes; we accept a second branch (an
op-type `switch`) for large uniform families, because it lives in a cold
handler that is warm on the inside, and it stops opcode proliferation at the
root. A new arithmetic-like operator becomes a `switch` case, not an opcode.

## Exactly one new argument type: `OPCOUNT`

The **only** new argument type is `OPCOUNT` -- a fusion of an operator selector
with a reducing **count**, both small, in one 16-bit `arg`:

```
arg = (op_type << 8) | count
      op_type : high byte, KosBinop (0..12)
      count   : low byte, reduction arity (operands folded from the stack)
```

Accessors in `bytecode.h`:

```c
#define KosOp_OPCOUNT_TYPE(insn)   ((KosBinop)(KosOp_ARG(insn) >> 8))
#define KosOp_OPCOUNT_COUNT(insn)  (KosOp_ARG(insn) & 0xff)
```

Nothing else is ever fused. `FASTVAR` and `VAR` keep their own full-width
slots. A family that needs more than op-type+count -- i.e. it also carries a
fast-slot or a var-name -- moves to an **`EXTRA`** row (the existing
multi-operand mechanism) whose fields are `OPCOUNT` plus a normal `FASTVAR` or
`VAR`. `OPCOUNT` is used directly as the main arg where op-type+count is all
that travels, and as one `EXTRA` field otherwise. One new style; everything
else reuses existing patterns.

## In-place is a reducing op (correctness fix)

Today the in-place handlers pop a single rhs -- they are binary. But
`(set! x (+ x a b c))` should fold *every* operand into `x`; the current
opcodes only express `x = x <op> rhs`, forcing the compiler to chain or
pre-reduce. Carrying a `count` makes the in-place variants **proper reducing
ops**: fold `count` operands off the stack into the target with the operator.
This unifies them with `OBJECT_<op>` reducing and removes the binary-only
limitation. All four families become "reduce `count` operands with `op_type`,
result to <target>", differing only in where the accumulator/result lives.

## Scope and encoding per family

Arithmetic operator set (`KosBinop`, 13 values, 4 bits): `ADD, SUBTRACT,
MULTIPLY, TRUE_DIVIDE, FLOOR_DIVIDE, MODULO, POWER, MATRIX_MULTIPLY, BIT_AND,
BIT_OR, BIT_XOR, BIT_LEFT, BIT_RIGHT`. Nine have an int-fast path
(`KosLongBinop`); the other four go straight to the `PyNumber` call. This is
only the arithmetic operators -- comparisons, predicates, `FORMAT`, etc. stay
as-is (comparisons already use `EXTRA`).

| family | operands | result to | encoding |
|---|---|---|---|
| reducing `OBJECT_<op>` | op-type + count | stack | **`OPCOUNT`** main arg -> `OBJECT_BINOP` |
| stack in-place `OBJECT_INPLACE_<op>` | op-type + count | stack (TOS accumulator) | `OPCOUNT` main arg -> `OBJECT_INPLACE_BINOP` |
| fast-slot `FAST_OBJECT_INPLACE_<op>` | op-type + count + **slot** | fast slot | **`EXTRA`** `[OPCOUNT, FASTVAR]` -> `FAST_OBJECT_INPLACE_BINOP` |
| var `VAR_OBJECT_INPLACE_<op>` | op-type + count + **name** | frame ns by name | **`EXTRA`** `[OPCOUNT, VAR]` -> `VAR_OBJECT_INPLACE_BINOP` |

Net: ~52 opcodes -> ~4. Removes ~48 opcodes (floor ~ -5.8 KB) and moves the
~52 handler bodies out of the dispatch function (~ -14 KB body), leaving four
tiny cold stubs. No index is ever byte-packed; VAR keeps full width in its
`EXTRA` slot (the earlier "VAR caveat" is gone).

## Hot / cold architecture (explicit)

- **Cold: the opcode handler.** `OBJECT_BINOP`, `FAST_OBJECT_INPLACE_BINOP`,
  etc. are `KOS_OP_HANDLER_COLD` (`static KOS_NOINLINE`). The dispatch function
  keeps only a one-line call per family (4 stubs vs 52 inlined bodies).
- **Hot / inlined: everything *inside* the cold handler.** The op-type `switch`
  and every case are fully inlined -- no per-op function, no function-pointer
  indirection. Each case is specialized: `kos_long_binop_fast(KOS_LBOP_<op>,
  ...)` with a *constant* (its internal switch collapses) then the `PyNumber_*`
  fallback, applied pairwise across the `count` fold. Warm inside, cold outside.
- **The dispatch loop** pays one computed-goto (unchanged) + one in-handler
  `switch` (the "second branch"). `EXTRA` families additionally read the row's
  `FASTVAR`/`VAR` field -- inline operand words, not a second dispatch.

### No helper-call layers: share cases by X-macro, not by call

The four families differ only in *prologue* (where the accumulator starts:
stack / fast slot / var name) and *epilogue* (where the result lands); the
operator `switch` over the `count` fold is identical. Share it at the *source*
level with an X-macro list, expanded inline into each handler -- zero call
layers:

```c
/* single source of truth for the operator set */
#define KOS_BINOP_LIST(X)                                                     \
  X(ADD,       KOS_LBOP_ADD,      PyNumber_Add,      PyNumber_InPlaceAdd)      \
  X(SUBTRACT,  KOS_LBOP_SUBTRACT, PyNumber_Subtract, PyNumber_InPlaceSubtract) \
  /* ... 9 more ... */                                                        \
  X(TRUE_DIVIDE, KOS_LBOP_NONE,   PyNumber_TrueDivide, PyNumber_InPlaceTrueDivide)

/* one inlined fold+switch, generated once, expanded per family */
#define KOS_BINOP_REDUCE(optype, acc, pop_next, count, INPLACE)  ...
```

Each family handler expands the reduce/switch once, wrapped by its own
prologue/epilogue. The operator logic lives once (the list); the code is
inlined per family (no runtime sharing, no call chain). Adding an operator =
one row.

The reducing family's existing int-fast shortcut (`obj_op_reducing_long`,
which today runtime-switches on a `lop` -- the very anti-pattern) is
**retired**: each `switch` case passes a *constant* `KOS_LBOP_*`, so the
int-fast path is specialized per case with no runtime `lop` re-branch.

## Compile-time validation (the new arg type)

`OPCOUNT` carries structured bits, so it needs validation the plain styles do
not:

- **op-type range**: high byte must be a defined `KosBinop` (0..12). Reject at
  emission and at code-load/soften (a bad value hits the `switch` `default`, a
  safe error, but we reject earlier).
- **count range**: low byte is the reduction arity; must fit a byte and match
  the operands actually on the stack. Emission refuses count > 255.
- **round-trip**: `serialize` -> `soften` preserves both bytes; add a
  validation assert in the soften path alongside the existing `EXTRA`-index
  bounds check.

The emitter (`ByteCodeFragment`) gains `instruction_opcount(op, op_type,
count)` (and an `EXTRA`-row variant for the fast/var families) that composes
and range-checks rather than passing a raw `arg`.

## Touch-point inventory

**Encoding / VM core (C)**
- `bytecode.h` -- add `KOSTYLE_OPCOUNT` to `KosOpStyle`; add `KosBinop` enum +
  `KosOp_OPCOUNT_*` accessors; extend the Python-exposed `KosOpStyleEnum`.
- `bytecode.c` -- `opcode_styles[]` picks it up via the `OP_3`/`OP_7` macros;
  audit every `switch(style)` / `st == KOSTYLE_*` site (`collect_label_map`,
  soften, arg-kind scans) so `KOSTYLE_OPCOUNT` is handled (no labels, no pool
  refs -> raw arg) and `EXTRA` rows with an `OPCOUNT` field validate. Add the
  round-trip assert.
- `opcodes.incl` -- delete the ~52 arithmetic `OP()` rows; add
  `OP(OBJECT_BINOP, 0, OPCOUNT)`, `OP(OBJECT_INPLACE_BINOP, 0, OPCOUNT)`,
  `OP(FAST_OBJECT_INPLACE_BINOP, 0, EXTRA, OPCOUNT, FASTVAR, NOARG, NOARG)`,
  `OP(VAR_OBJECT_INPLACE_BINOP, 0, EXTRA, OPCOUNT, VAR, NOARG, NOARG)`.
- `longops.h` -- add `KOS_LBOP_NONE` sentinel for the four non-int-fast ops.

**Eval (C)**
- `koskari/lib/opcode/` -- add the four `*_BINOP.c` cold handlers
  (`KOS_OP_HANDLER_COLD`) with the inlined reduce/switch; delete the ~52 old
  per-operator handler files.
- `handlers.incl` -- regenerated by `tox -e handlers` (`tools/opimpl.py`);
  fewer includes.
- `eval/eval_utils.c` -- `KOS_BINOP_LIST` / reduce macros + a
  `kos_binop_bad_optype` cold error helper; retire `obj_op_reducing_long`'s
  `lop` switch.

**Compiler (Python)**
- `compile/specials/object.py` -- `_reduce_op`, `_binary_op`, `_unary_op` emit
  the `*_BINOP` opcode with an op-type + count; the in-place op-symbol tables
  (`VAR_OBJECT_INPLACE_*`, `FAST_OBJECT_INPLACE_*`) map operator symbol ->
  `KosBinop`, and emit reducing count (the binary-only special-casing goes
  away).
- `compile/compiler.py` -- `ByteCodeFragment.instruction_opcount` + `EXTRA`-row
  emit; range-check the packed value.
- fast/var in-place fusion (`compiler.py` / `specials/object.py` /
  `pragma/resolution.py`) -- emit `FAST_OBJECT_INPLACE_BINOP` /
  `VAR_OBJECT_INPLACE_BINOP` `EXTRA` rows with `[OPCOUNT, FASTVAR|VAR]`.
- op-type constant table: single source shared C<->Python (expose `KosBinop`).

**Tooling (Python)**
- `dis.py` -- `_format_arg`: `KOSTYLE_OPCOUNT` case rendering `"<OP> xN"` (e.g.
  `OBJECT_BINOP BIT_XOR x2`), and `OPCOUNT` inside an `EXTRA` row. Keeps
  human-readable per-operator disassembly.
- `dis_ng.ki` -- mirror.
- `serialize/codex.py` -- `intern_instruction_args` / `encode`/`decode`:
  `KOSTYLE_OPCOUNT` is a plain `uint16`, no pool interning (like `COUNT`), but
  handled explicitly in the `arg_kind` wire path; `EXTRA` rows already covered.

**Observability / tracing**
- `tracing.c` + opcode-timing -- collapsing merges a family's per-operator
  buckets. Sub-bucket by `op_type` for the `*_BINOP` families so per-operator
  timing (the data this effort steers by) survives. Recommended.

**Tests / corpus**
- Compile-phase expectations in `docs/corpus/records/` and tests asserting
  arithmetic opcodes / dis output change; update alongside. The in-place
  reducing fix may also *add* newly-correct behavior to cover.

## Risks & open questions

1. **The op-type switch misprediction (the gate).** The `switch` is at one
   address but polymorphic across call sites; monomorphic in a single-op loop,
   alternating in mixed arithmetic (`churn_loop`). Modern TAGE may learn a
   repeating loop pattern. **Must measure**: prototype
   `FAST_OBJECT_INPLACE_BINOP` (and `VAR_` to prove the `EXTRA` path) and
   cachegrind `churn_loop` (mixed ops = worst case) vs plain `_COLD`. Gate the
   plan on this.
2. **Reducing semantics for in-place** -- fold order and stack layout must
   match the old chained behavior exactly; cover with corpus/reproducers.
3. **Tracing granularity** -- take the op-type sub-bucketing so per-operator
   visibility survives.
4. **Compile-time validation** must be airtight -- a silent count/op-type
   truncation would be a correctness bug; the emitter range-check is
   load-bearing.
5. **Cross-language op-type drift** -- `KosBinop` values must match C and the
   Python emitter; single source.

## Phased rollout

- **Phase 0 (gate)** -- prototype `FAST_OBJECT_INPLACE_BINOP` +
  `VAR_OBJECT_INPLACE_BINOP` (both via `EXTRA[OPCOUNT, FASTVAR|VAR]`) end to
  end (style, `KosBinop`, accessors, validation, two cold handlers, compiler
  emit, dis render), and cachegrind `churn_loop` vs plain `_COLD`. Go/no-go on
  the switch cost.
- **Phase 1** -- land `KOSTYLE_OPCOUNT`, `KosBinop`, `KOS_BINOP_LIST`, the
  fast/var in-place families as reducing ops; full suite + ASan + dispatch-size
  + cachegrind.
- **Phase 2** -- migrate the stack cases (`OBJECT_<op>` reducing,
  `OBJECT_INPLACE_<op>`) to `OBJECT_BINOP` / `OBJECT_INPLACE_BINOP`; remove the
  operand-specific opcodes; retire `obj_op_reducing_long`'s `lop` switch.
- **Phase 3** -- tracing sub-buckets; corpus/test updates.

Remeasure dispatch-fn size, floor, suite, ASan, cachegrind after each phase.
Record deltas against `design/dispatch-sizing.md`.

<!-- Companion: design/dispatch-sizing.md (sizing baseline + un-inline-by-size rule). -->
