# ByteCodeProcessor: the unified bytecode transformation submodule

The pre-finalize instruction transformations -- peephole squish, feeder
fusion, VAR run packing, operand normalization, VAR to FAST lowering,
and generator tail-call demotion -- live in `ByteCodeProcessor` in the
**single submodule `koskari/compile/processor.py`**: the class
definition and all implementation facets in one module, not a
subpackage. `Compiler.complete()` invokes it once; the compiler holds
no other knowledge of the pipeline. Phases 1-3 below are **built**;
phase 4 (tail-call promotion) remains.

## What the original code taught

The pre-migration pipeline contained three kinds of transform with
different data-driven potential:

- **Uniform relabelings** were already data-driven and proved the
  approach: `convert_fast` lowers through the name-derived `VAR_TO_FAST`
  map plus the EXTRA schemas (a new fused op costs zero compiler code).
- **Feeder absorptions** shared real machinery -- backward scanning,
  splicing, POSITION transparency -- but each rule had bespoke argument
  surgery (cmpsrc packing, ternary splitting, the VAR_NEXT default-row
  reorder).
- **Peepholes** were each a few lines of irregular logic behind a long
  elif chain.

Lesson: the matching side generalizes; the rewrite side mostly does
not. The built system reflects this: forms are declared, rewrites are
code.

## Lifecycle: a use-and-discard mutable box

The processor is not a persistent peer of `Compiler`; it is closer to
`ByteCodeFragment` -- constructed per instruction stream, used once,
discarded:

- **Rules live at module level.**  The registries and their pattern
  tables are built once at import time and shared by every instance.
  That is the stateless part.
- **The instance is the mutable box.**  It internalizes the incoming
  rows at construction and mutates itself as passes apply. Everything
  else it carries is per-stream state with no meaning beyond one code
  object: the working row list, and later the label index and extent
  stack. Lifetime == state validity: no reset method to forget, no
  cross-stream leakage possible by construction.
- **Ingest, mutate, emit.**  `process(resolve_nspec)` runs the gated
  pipeline and returns the finished rows for Code construction. The
  namespace spec is not knowable until the compressed stream has been
  scanned for inner code, so the caller supplies `resolve_nspec`, a
  callback receiving the compressed rows and returning the base
  NamespaceSpec for the fast lowering -- the closure/write merging
  semantics stay compiler-side.

## Pragma gating

The passes are pragma-gated, and the gates stay positionally correct:
pragma state affects the code after it in a module, so each code
object is processed under the gates in effect at its compile point.
The use-and-discard lifecycle delivers this for free -- the gate
snapshot is taken at construction and is consistent for the whole run.

- `Compiler` resolves the pragma into a per-pass flags bundle
  (`convert-fast` today; `tco` later) and constructs the processor
  with rows, flags, and mode. `process()` checks the gates internally.
- **The processor never holds a `Compiler` reference.**  It defines
  which flags it consumes; the compiler supplies them. Coupling stays
  one-directional, and the pass-level unit tests construct a processor
  from a row list alone -- no `Compiler`, no `Module`.

## Architecture: declared forms, coded rewrites

A pass is a rule registry over the shared driver. Rules declare their
**form** and code their **rewrite**:

- **Pattern declarations.**  `@squish_rule(...)` / `@fuse_rule(...)`
  take the whole form in stream order: lookback slot specs (a bare
  opcode, or a set such as `FEEDER`) followed by the trigger opcode.
  The decorator line reads as the sequence being compressed:
  `@fuse_rule(OpCode.VAR, OpCode.OBJECT_ITER)`. A trigger-only pattern
  registers a fallback rule that inspects the working rows itself.
- **Dispatch.**  Each registry maps trigger opcode to its candidate
  (priors, rule) list. The driver tries candidates in registration
  order; the first whose form structurally matches (via
  `match_priors`, POSITION-transparent) *and* whose rule accepts is
  applied -- the same match-then-accept shape as the specials. A rule
  declining (returning None) passes the form to the next candidate,
  which is how arity variants of one trigger route: each is its own
  declared rule with its own arg guard.
- **The rule protocol.**  A rule receives the matched prior rows in
  pattern order and returns one of three things: **None** to decline;
  **a list of rows** as its replacement, which the driver splices over
  the matched form; or **a rewritten (op, arg) trigger** -- the
  matched rows are absorbed and dispatch retries on the new trigger,
  descending the lowering ladder. Termination is the rule author's
  obligation: every rewrite must consume rows or produce an op that
  resolves without further rewriting.
- **The lowering ladder, live.**  The format chain is the worked
  example: `CONST OBJECT_FORMAT(2)` demotes to `OBJECT_FORMAT_CONST`
  via retry, whose own rule fuses a bare VAR object into
  `VAR_FORMAT_CONST`, and `convert_fast` carries it to
  `FAST_FORMAT_CONST` -- four representations across three passes, no
  rule aware of more than its own step, no combination enumerated
  anywhere.
- **Services.**  `match_priors` (the backward form matcher) and
  `splice` (region replacement with re-emission of the latest
  stepped-over position row) are the shared machinery every rule rides.

**Passes stay distinct.**  compress = squish, then `fuse_feeders`,
then `pack_var_runs`, in that order so fusion gets first claim on
feeder rows; then `normalize_opcount`, the gated `convert_fast`, and
`demote_tail_calls_if_yields`. Passes remain independently callable so
tests target them individually. `pack_var_runs` keeps its bespoke
forward run-accumulation -- it is not lookback-shaped.

## Invariants

- **POSITION is ethereal.**  Every backward scan steps over POSITION
  rows -- the matcher, and the squish trailer's RETURN normalization
  alike. The trailer earned its mention: it was the last literal
  last-row check in the file, and when the replacement protocol let a
  stream end on a re-emitted position row, it appended a dead RETURN
  that only the fingerprint's basic-block count caught. Splice re-emits
  the latest stepped-over position after the rewritten rows so
  following instructions keep their attribution; Code construction
  canonicalizes position order beyond that.
- **LABEL is an absolute lookback boundary.**  A label is a merge
  point: something lands there in a manner whose prior state cannot be
  predicted. No backward-looking rule, including any future
  const-tracking or stack-shape analysis, reasons through a LABEL.
  Reasoning about the join of all predecessors is dataflow analysis --
  a different tool this pipeline does not want. Forward reasoning
  *through* labels stays legal (see tail calls), because it asks what
  happens after control arrives, not what state it arrived with.
- **Representation vs semantics.**  Passes own representation
  transforms (const-ness detection, fusion, demotion); specials keep
  owning meaning. Worked example from the format ladder: a quoted
  symbol as format rules means "its string" -- that coercion stays in
  `special_format`, along with the all-constant compile-time fold whose
  failure is a diagnostic naming the source form. A pass must never
  need to know what a form *means*.

## Future services

**Label refcounting and index.**  One linear pass collecting every
label operand from every jump-carrying row -- schema-driven, since
LABEL is a declared operand kind in `opcodes.incl` styles and EXTRA
schemas, so no hand-maintained list of jump ops. Yields the
label -> row index (needed by tail-call forward chasing) and reference
counts.

**Dead-region sweep and label GC** (optional, deferred).  With
refcounts, unreachable-code trimming is a scan with a mode bit, not a
CFG: after an unconditional transfer (JUMP, RETURN, RAISE, BREAK,
CONTINUE), rows up to the next label with nonzero refcount are dead
and deletable -- which is what actually reclaims constants from the
pool, since pooling interns only what survives to finalize. A label
whose count drops to zero is itself removable, unblocking rules and
run packing that break at LABEL rows. Structured emission implies a
dead region contains balanced PUSH_*/POP_* pairs; the extent tracker
asserts this and refuses a deletion that would straddle a pair.
Repeatable-segment detection is genuinely graph-shaped and stays out
of scope.

**Extent tracker.**  A push/pop stack of open extents maintained
during the walk. Sound as a linear scan because the emitter generates
structured code (jumps do not cross extent boundaries); verified
safetynet-style during the pass rather than trusted, so a miscompiled
extent fails loudly at compile time instead of distantly at runtime.
Open questions: CLEAR_* semantics against the stack (restart vs
close), and unifying this computation with whatever finalize already
does to produce the Code object's extent metadata, so nesting is
computed once.

## The tail-call promotion pass (phase 4, remaining)

The strongest motivation for the processor -- the same principle as
"opcodes are thin interfaces": the pass owns the optimization, specials
emit naive forms. Detail lives in
[language/tail-call-bytecode-pass.md](language/tail-call-bytecode-pass.md);
the processor-specific mechanics:

- Specials always emit `CALL*` + `RETURN`; the `tc=` parameter threading
  through every special signature is deleted. Note the un-fusion
  obligation: today `tc` suppresses the CALL_VAR fusion
  (`use_var_path = is_symbol(callable) and not tc`), so a
  tail-position `CALL_VAR` must rewrite to the bare load plus
  TAIL_CALL to reproduce current emission, before the fast lowering.
- The pass is one linear read maintaining the extent stack from the
  push/pop rows. At each promotable call: if **any** frame of the
  stack is a disallowed extent, denied. Otherwise read forward --
  allowed route rows are extent pops, LABEL, POSITION, and NOOP;
  unconditional JUMPs are followed through the label index with a
  seen-set guarding cycles -- and promote **only on arrival at
  `RETURN 1`**. RETURN 0 discards the value and RETURN 2+ is a
  different shape; any other row invalidates, and the scan continues
  at the next call. This catches what syntactic threading cannot:
  explicit returns, and arms jumping to a shared RETURN.
- The extent table is data beside the pass, decided by one principle:
  **an extent whose uninstall behavior must run after the call cannot
  be tail-called through.** Disallowed: try, finally, with, dyn-with,
  wind, prompt/reset, shift, call/cc, param(eterize), collect.
  Discardable (allowed): namespace and loop/foreach extents, safely
  dropped by frame handoff.
- The rewrite touches the call row only. The JUMP and the shared
  RETURN stay: other predecessors may land on that label with non-tail
  values. Rows unreachable on the lowered path are harmless (or food
  for the dead-region sweep).
- Yield-ness gates the whole pass up front, per the TODO entry: scan
  for YIELD / YIELD_FROM first, skip promotion entirely for yielding
  streams, never convert-then-unconvert.

### Tail opcode parity worksheet

The specials will stop emitting TAIL forms entirely; promotion in the
processor then needs a TAIL twin for every call op the call specials
can emit, so that promotion is always a pure relabel carrying the same
operands and stack contract, with only the frame handoff differing.
The historical gap was election, not impossibility: a tail form of a
var or fast call stores the lookup and the arguments before resetting
the frame. Current state of the family:

| call op | tail twin | status |
|---|---|---|
| CALL | TAIL_CALL | exists |
| CALL_NAMES | TAIL_CALL_NAMES | exists |
| CALL_STAR | TAIL_CALL_STAR | exists |
| CALL_VAR | TAIL_CALL_VAR | needed |
| CALL_VAR_NAMES | TAIL_CALL_VAR_NAMES | needed |
| CALL_VAR_STAR | TAIL_CALL_VAR_STAR | needed |
| CALL_FAST | TAIL_CALL_FAST | needed |
| CALL_FAST_NAMES | TAIL_CALL_FAST_NAMES | needed |
| CALL_FAST_STAR | TAIL_CALL_FAST_STAR | needed |
| CALL_ATTR | TAIL_CALL_ATTR | needed |
| CALL_ATTR_NAMES | TAIL_CALL_ATTR_NAMES | needed |
| CALL_ATTR_STAR | TAIL_CALL_ATTR_STAR | needed |
| CALL_VAR_ATTR | TAIL_CALL_VAR_ATTR | needed |
| CALL_VAR_ATTR_NAMES | TAIL_CALL_VAR_ATTR_NAMES | needed |
| CALL_VAR_ATTR_STAR | TAIL_CALL_VAR_ATTR_STAR | needed |
| CALL_FAST_ATTR | TAIL_CALL_FAST_ATTR | needed |
| CALL_FAST_ATTR_NAMES | TAIL_CALL_FAST_ATTR_NAMES | needed |
| CALL_FAST_ATTR_STAR | TAIL_CALL_FAST_ATTR_STAR | needed |

Fifteen new opcodes for full parity: the VAR and FAST tiers of the
plain family, and the nine attr forms. The attr tier reverses the old
"attr calls never tail-call" election recorded in `compile/call-attr`.
Each twin takes exactly its CALL form's operand schema. Adding them
renumbers nothing if appended within their families, but any insertion
plan carries the usual bytecode ABI minor bump.

Wiring notes:

- The promotion table grows one relabel entry per pair; no un-fusion
  or stack surgery anywhere.
- The VAR to FAST lowering pairs (TAIL_CALL_VAR -> TAIL_CALL_FAST and
  the attr and names/star variants) do not register through the
  name-driven map as written: `build_var_to_fast` keys on the `FAST` /
  `CALL_FAST` name prefixes and will not see `TAIL_CALL_FAST*`. Either
  the builder grows the `TAIL_CALL_FAST` prefix or the pairs are added
  explicitly.
- Promotion ordering relative to the fast lowering becomes free once
  both tiers exist: promoting before lowering relabels the VAR forms
  and lowering carries them to FAST twins, or promotion after lowering
  relabels the FAST forms directly. Either works when the map is
  complete.
- During the transition, with the specials' tc pinned False and parity
  not yet built, tests that assert TAIL emission for the fused call
  shapes are expected to fail; they check the old election too
  closely and settle once the twins land.

Tail-call promotion changes observable behavior (backtrace shape,
frame identity), so it lands in two steps: (a) the pass reproduces
exactly the promotions the specials make today -- `tc=` threading
deleted, bootstrap bytecode diff identical, gated by
`Compiler.tco_enabled`; (b) the eligibility extension to join-point
shapes and the extent table, as genuinely new behavior with its own
review. The extent tracker's safetynet verification must be in place
and exercised before (b). Each service arrives with its consumer:
label index and extent tracker with this pass, dead-region sweep and
label GC last, if at all.

## Simplifications delivered and available

`compile/format` is the landed worked example: it folds the
all-constant case (keeping the compile-time diagnostic and the
quoted-symbol coercion) and otherwise emits naively; the pass ladder
owns everything below. The same pattern is available to any special
still branching on operand const-ness at emission, retiring
per-special `const_only_expr` duplication where the fold is not itself
a diagnostic.

## Verification: emission fingerprints

Every behavior-preserving step gets an objective gate beyond the test
suite. The opgrams corpus compiles bootstrap, modules, life, churn,
and tools; `tox -qe opgrams -- --json` is a whole-corpus emission
fingerprint, diffed structurally after each step. When a step is
*supposed* to change emission, the fingerprint diff is the review
artifact instead. Lesson learned: read the **blocks** count, not just
grams and successors -- a dead single-op block after a boundary is
invisible to both adjacency tables, and the block count is what caught
the trailer bug during phase 3.

## Performance stance

The budget is bootstrap compile time at package import, measured as
`import koskari.builtins` in a fresh process (`import koskari` alone
is deferred and compiles nothing). Measured 2026-08-19, five runs
each: pre-migration master steady ~0.238s (min 0.236), the completed
phase-3 processor steady ~0.235s (min 0.232) -- parity within noise.
The dict-dispatch-beats-elif claim did not materialize at this scale,
and neither did any cost from the matcher, candidate lists, or
replacement splices. The registry stands on sight-readability, not
speed; re-measure only if a future pass (TCO's label index and extent
tracking) grows the walk.

## Build record

1. **The heave** -- passes relocated from `compiler.py` into the
   submodule; single gated `process()` with the `resolve_nspec`
   callback; `Compiler` initially kept thin delegates, later removed
   along with the pass-level test modules' compiler dependence.
2. **Machinery** -- shared backward scanning and splice on the
   instance; the peephole rules flipped onto POSITION transparency as
   a separately fingerprinted step (fourteen corpus sites gained the
   CONST None / POSITION / RETURN normalization).
3. **Pattern registries** -- both passes converted to form-declaring
   rules over one driver with the replacement-return protocol and
   retry ladder; the format demotion landed with `special_format`'s
   simplification; the fusion pass renamed `fuse_feeders`. Emission
   verified unchanged throughout, except where the change was the
   point.
4. **TCO** -- remaining; see above.
