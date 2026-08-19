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
  through every special signature is deleted.
- Detection is a forward walk from the call row accepting POSITION
  rows, LABELs, and jump-chasing (a JUMP whose label row leads to
  another JUMP, as nested conditionals produce), succeeding only on
  arrival at `RETURN 1`. Arrival at anything else -- including
  `RETURN 0`, which discards the value -- disqualifies. This uniformly
  catches shapes the per-special approach structurally cannot, e.g. a
  conditional whose arms each end in a call jumping to a shared RETURN.
- Eligibility reads the extent stack at the call site (which matches
  the state at the label, by structured emission) against a data table
  of tail-blocking extents: try / finally / with / wind / prompt
  require cleanup or chain discipline and block promotion. That table
  lives beside the opcode declarations, not inside pass logic.
- The rewrite touches the call row only. The JUMP and the shared
  RETURN stay: other predecessors may land on that label with non-tail
  values. Rows unreachable on the lowered path are harmless (or food
  for the dead-region sweep).
- Yield-ness gates the whole pass up front, per the TODO entry: scan
  for YIELD / YIELD_FROM first, skip promotion entirely for yielding
  streams, never convert-then-unconvert.

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
