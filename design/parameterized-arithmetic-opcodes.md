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
parameterized opcode carrying an op-type operand, dispatched cold, with the
per-operator work inlined *inside* that cold handler.

This is a deliberate, scoped exception to Koskari's early-fork idiom (one
distinct opcode per variation, selection resolved by the computed goto). We
keep early-fork for hot standalone opcodes; we accept a second branch (an
op-type `switch`) for large uniform families, because it lives in a cold
handler that is warm on the inside, and it stops opcode proliferation at the
root. A new arithmetic-like operator becomes a `switch` case, not a new
opcode.

## Scope

Collapse the arithmetic operators of four families. Comparisons, predicates,
`FORMAT`, and other `OBJECT_*` ops are out of scope (comparisons already use
`EXTRA`).

| family | current opcodes | style today | collapses to |
|---|---|---|---|
| reducing (`OBJECT_<op>`) | ~13 | `COUNT` | `OBJECT_BINOP` |
| stack in-place (`OBJECT_INPLACE_<op>`) | ~13 | `COUNT`/`NOARG` | `OBJECT_INPLACE_BINOP` |
| fast-slot in-place (`FAST_OBJECT_INPLACE_<op>`) | ~13 | `FASTVAR` | `FAST_OBJECT_INPLACE_BINOP` |
| var in-place (`VAR_OBJECT_INPLACE_<op>`) | ~13 | `VAR` | `VAR_OBJECT_INPLACE_BINOP` (see VAR caveat) |

Arithmetic operator set (the op-type sub-enum), 13 values, fits in 4 bits:
`ADD, SUBTRACT, MULTIPLY, TRUE_DIVIDE, FLOOR_DIVIDE, MODULO, POWER,
MATRIX_MULTIPLY, BIT_AND, BIT_OR, BIT_XOR, BIT_LEFT, BIT_RIGHT`. Nine have an
int-fast path (`KosLongBinop`); the other four (`TRUE_DIVIDE`,
`FLOOR_DIVIDE`, `POWER`, `MATRIX_MULTIPLY`) go straight to the `PyNumber`
call.

Net: ~52 opcodes -> ~4. Removes ~48 opcodes (floor ~ -5.8 KB) and moves the
~52 handler bodies out of the dispatch function (~ -14 KB body), leaving four
tiny cold stubs.

## Encoding: the packed operand

`KosOp` is `uint16 op + uint16 arg`. The new operand packs two bytes into the
single 16-bit `arg`:

```
arg = (op_type << 8) | operand
      op_type : high byte, the KosBinop value (0-12)
      operand : low byte, the family's existing small operand
                COUNT  (reducing/inplace: arg count, ~2)  -- fits a byte
                FASTVAR (fast-slot index, <= 128)          -- fits a byte
```

New style `KOSTYLE_OPPACK` (working name), with accessors in `bytecode.h`:

```c
#define KosOp_OPPACK_TYPE(insn)     ((KosBinop)(KosOp_ARG(insn) >> 8))
#define KosOp_OPPACK_OPERAND(insn)  (KosOp_ARG(insn) & 0xff)
```

No second dispatch: the op-type rides in the same instruction word the
computed goto already fetches. One fetch, one dispatch, one in-handler
`switch`.

### VAR caveat

`VAR` operands are name-pool indices that can exceed 255, so
`VAR_OBJECT_INPLACE_BINOP` cannot byte-pack. Two acceptable options,
decided at implementation:

1. Encode it with the existing `EXTRA` row mechanism (op-type in one slot,
   VAR index in another) -- inline operand words, still one dispatch.
2. Leave `VAR_OBJECT_INPLACE_<op>` un-collapsed. VAR arithmetic is cold and
   rare (the compiler prefers `FAST_*`); its bytes matter least. Simplest.

Recommendation: option 2 first (leave VAR alone), revisit with `EXTRA` only
if its ~4.8 KB proves worth the added encoding surface.

## Hot / cold architecture (explicit)

- **Cold: the opcode handler.** `OBJECT_BINOP`, `FAST_OBJECT_INPLACE_BINOP`,
  etc. are declared `KOS_OP_HANDLER_COLD` (`static KOS_NOINLINE`). The
  dispatch function keeps only a one-line call per family (4 stubs vs 52
  inlined bodies).
- **Hot / inlined: everything *inside* the cold handler.** The op-type
  `switch` and every case are fully inlined in the handler -- no call out to a
  per-op function, no function-pointer indirection. Each case is the
  specialized body: `kos_long_binop_fast(KOS_LBOP_<op>, ...)` with a *constant*
  (its internal switch collapses, stays specialized) then the `PyNumber_*`
  fallback. Warm inside, cold outside.
- **The dispatch loop** pays: one computed-goto (unchanged) + one in-handler
  `switch`. The `switch` is the "second branch."

### No helper-call layers: share cases by X-macro, not by call

The four families differ only in *prologue* (where the operands come from)
and *epilogue* (where the result goes); the operator `switch` body is
identical. Share it at the *source* level with an X-macro list, expanded
inline into each handler -- zero call layers:

```c
/* longops-style single source of truth for the operator set */
#define KOS_BINOP_LIST(X)                                        \
  X(ADD,       KOS_LBOP_ADD,      PyNumber_Add,      PyNumber_InPlaceAdd)      \
  X(SUBTRACT,  KOS_LBOP_SUBTRACT, PyNumber_Subtract, PyNumber_InPlaceSubtract) \
  /* ... 11 more ... */                                          \
  X(TRUE_DIVIDE, KOS_LBOP_NONE,   PyNumber_TrueDivide, PyNumber_InPlaceTrueDivide)

/* one inlined switch, generated; INPLACE picks column 4, reducing column 3 */
#define KOS_BINOP_SWITCH(optype, l, r, out, COL)                 \
  switch (optype) {                                              \
    KOS_BINOP_LIST(KOS_BINOP_CASE_##COL)                         \
    default: return kos_binop_bad_optype(optype);                \
  }
```

Each family handler expands `KOS_BINOP_SWITCH` once, sandwiched between its
own prologue/epilogue. The operator logic lives once (the list); the code is
inlined per family (no runtime sharing, no call chain). Adding an operator =
one row in `KOS_BINOP_LIST`.

The reducing family's int-fast shortcut (`obj_op_reducing_long`, which today
runtime-switches on a `lop` -- the anti-pattern) is *retired*: each `switch`
case passes a *constant* `KOS_LBOP_*`, so the int-fast path is specialized
per case with no runtime `lop` re-branch. Reducing and in-place share the
operator list but keep distinct, inlined prologue/epilogue -- clean, no new
call layers.

## Compile-time validation (new arg type)

`KOSTYLE_OPPACK` needs validation the other styles do not, because it carries
structured bits:

- **op-type range**: high byte must be a defined `KosBinop` (0..12). Reject at
  compile emission and at code-load/soften.
- **operand range**: low byte must fit -- `FASTVAR` slot < 128, `COUNT` within
  the family's max. Emission must refuse an operand > 255 (compiler bug guard).
- **round-trip**: `serialize` -> `soften` must preserve both bytes; add a
  validation assert in the soften path (`bytecode.c`) alongside the existing
  `EXTRA`-index bounds check.

The emitter (`ByteCodeFragment.instruction`) gains a helper,
`instruction_binop(op, op_type, operand)`, that composes and range-checks the
packed value rather than passing a raw `arg`.

## Touch-point inventory

**Encoding / VM core (C)**
- `koskari/lib/bytecode.h` -- add `KOSTYLE_OPPACK` to `KosOpStyle`; add
  `KosBinop` enum + `KosOp_OPPACK_*` accessors; extend `KosOpStyleEnum`
  (Python-exposed) so tooling sees the new style.
- `koskari/lib/bytecode.c` -- `opcode_styles[]` picks it up via the `OP_3`
  macro automatically; audit every `switch (style)` / `st == KOSTYLE_*`
  site (`collect_label_map`, soften, any arg-kind scan) to handle/skip
  `KOSTYLE_OPPACK` (it carries no pool refs and no labels, so most default to
  "leave the raw arg"). Add the round-trip validation assert.
- `koskari/lib/opcodes.incl` -- delete the ~52 arithmetic `OP()` rows; add the
  ~4 `OP(<FAMILY>_BINOP, 0, OPPACK)` rows. This shrinks the opcode enum and
  the styles table.
- `koskari/lib/longops.h` -- extend `KosLongBinop` with a `KOS_LBOP_NONE`
  sentinel for the four non-int-fast operators (or gate them in the macro).

**Eval (C)**
- `koskari/lib/opcode/` -- add `OBJECT_BINOP.c`, `OBJECT_INPLACE_BINOP.c`,
  `FAST_OBJECT_INPLACE_BINOP.c` (+ VAR per caveat), each a
  `KOS_OP_HANDLER_COLD` with the inlined `KOS_BINOP_SWITCH`. Delete the ~52
  old per-operator handler files.
- `koskari/lib/handlers.incl` -- regenerated by `tox -e handlers`
  (`tools/opimpl.py`); fewer `#include`s. Confirm opimpl needs no change
  beyond reading the new `opcodes.incl`.
- `koskari/lib/eval.c` -- the `KOS_BINOP_LIST` / `KOS_BINOP_SWITCH` macros and
  the `kos_binop_bad_optype` cold error helper live in an eval include
  (`eval/eval_utils.c`); retire `obj_op_reducing_long`'s runtime-`lop` switch.

**Compiler (Python)**
- `koskari/compile/specials/object.py` -- `_reduce_op`, `_binary_op`,
  `_unary_op` emit the `*_BINOP` opcode with a packed operand and an
  op-type mapping (`ADD -> KosBinop.ADD`) instead of distinct opcodes; keep
  constant-folding intact.
- `koskari/compile/compiler.py` -- `ByteCodeFragment.instruction` /
  `instruction_binop`: compose + range-check the packed operand.
- fast-slot in-place fusion (the pass that recognizes `set! x (op x y)` and
  emits `FAST_OBJECT_INPLACE_*`; in `compiler.py` / `specials/object.py` /
  `pragma/resolution.py`) -- emit `FAST_OBJECT_INPLACE_BINOP` with packed
  `(op-type, slot)`.
- op-type constant table shared between compiler and C (single source; expose
  `KosBinop` to Python or mirror it once).

**Tooling (Python)**
- `koskari/dis.py` -- `_format_arg`: add a `KOSTYLE_OPPACK` case that unpacks
  and renders `"<OP> operand"` (e.g. `OBJECT_BINOP BIT_XOR, 2`). Preserves
  human-readable per-operator disassembly.
- `koskari/dis_ng.ki` -- mirror the rendering.
- `koskari/serialize/codex.py` -- `intern_instruction_args` /
  `encode_instruction` / `decode_instruction`: `KOSTYLE_OPPACK` is a plain
  `uint16` with no pool interning (like `COUNT`), but must be handled
  explicitly in the `arg_kind` wire path.

**Observability / tracing**
- `koskari/lib/tracing.c` + opcode-timing -- today per-op timing keys on the
  opcode; collapsing merges all operators of a family into one bucket. Decide:
  (a) accept coarser buckets (`OBJECT_BINOP` aggregate), or (b) sub-bucket by
  op-type for the `*_BINOP` families so per-operator timing survives. (b) keeps
  the data this very effort steers by; it is a bounded change to the timing
  record + report. Recommend (b).

**Tests / corpus**
- Compile-phase expectations in `docs/corpus/records/` and any test asserting
  specific arithmetic opcodes / dis output will change; update alongside.

## Risks & open questions

1. **The op-type switch misprediction (the crux).** The `switch` sits at one
   address but targets vary by call site (polymorphic). Monomorphic in a
   single-op loop; alternating in mixed arithmetic (`churn_loop`). Modern TAGE
   may learn a repeating loop pattern. **Must measure** before committing:
   prototype `FAST_OBJECT_INPLACE_BINOP` alone and cachegrind `churn_loop`
   (mixed ops = worst case) against the plain `_COLD` un-inline. Gate the whole
   plan on this result.
2. **VAR variant encoding** -- byte-pack overflow; default to leaving VAR
   un-collapsed (see caveat).
3. **Tracing granularity** -- pick (b) sub-bucketing to keep per-operator
   visibility; without it we lose the profile that justified this work.
4. **Compile-time op-type/operand validation** must be airtight -- a bad high
   byte would index the `switch` `default` (safe error) but a silent operand
   truncation would be a correctness bug; the emitter range-check is
   load-bearing.
5. **Cross-language op-type table drift** -- the `KosBinop` values must match
   between C and the Python emitter; single source (expose the C enum) to
   avoid skew.

## Phased rollout

- **Phase 0 (gate)** -- prototype one family (`FAST_OBJECT_INPLACE_BINOP`)
  end to end (encoding, one cold handler, compiler emit, dis render) and
  cachegrind `churn_loop` vs plain `_COLD`. Decide go/no-go on the switch cost.
- **Phase 1** -- land `KOSTYLE_OPPACK`, `KosBinop`, accessors, validation, and
  the `KOS_BINOP_LIST` macro; convert the `FAST_OBJECT_INPLACE` family; full
  suite + ASan + dispatch-size + cachegrind.
- **Phase 2** -- convert the reducing (`OBJECT_`) and stack in-place
  (`OBJECT_INPLACE_`) families; retire `obj_op_reducing_long`'s `lop` switch.
- **Phase 3** -- tracing sub-buckets; corpus/test updates; optional VAR via
  `EXTRA`.

Remeasure dispatch-fn size, floor, suite, ASan, and a cachegrind spot-check
after each phase. Record deltas against `design/dispatch-sizing.md`.

<!-- Companion: design/dispatch-sizing.md (sizing baseline + un-inline-by-size rule). -->
