# ByteCodeProcessor: a unified bytecode transformation submodule

Design for migrating the pre-finalize instruction transformations --
`compress`, `fuse_check_feeders`, `pack_var_runs`, `normalize_opcount`,
`convert_fast`, and the future tail-call promotion -- out of `Compiler`
methods into a dedicated class, `ByteCodeProcessor` (name provisional),
living in a **new submodule of the compiler package**
(`koskari/compile/processor.py` or similar). `Compiler` delegates to it;
the passes leave `compiler.py`.

## What the current code teaches

The existing pipeline contains three kinds of transform with different
data-driven potential:

- **Uniform relabelings** are already data-driven and prove the
  approach: `convert_fast` lowers through the name-derived `VAR_TO_FAST`
  map plus the EXTRA schemas (a new fused op costs zero compiler code),
  and `_CHECK_SQUASHES` / `_SWITCH_SQUASHES` are the same idea in
  miniature.
- **Feeder absorptions** (`fuse_check_feeders`) share real machinery --
  `scan_back`, `splice`, POSITION transparency -- but each rule has
  bespoke argument surgery (cmpsrc packing, ternary splitting, the
  VAR_NEXT default-row reorder).
- **Peepholes** (`compress`) are each a few lines of irregular logic
  behind a long elif chain.

Lesson: the matching side generalizes; the rewrite side mostly does
not.

## Architecture

**Keep rewrites as code; make dispatch and lookback data-driven.**  No
pattern DSL -- expressing "N feeders, per-slot op constraints, arg
guards, then compute this tuple" declaratively is just functions with
extra ceremony. Instead:

- A dispatch dict keyed by trigger opcode, each rule a small registered
  method. This removes the elif chains and is likely *faster*: today
  every instruction walks up to a dozen elif tests that mostly miss; a
  dict hit is one hash.
- Shared services owned by the processor: a generalized lookback with a
  configurable transparency set, `splice` with POSITION bookkeeping,
  the extent tracker, and label indexing (below).
- **Re-dispatch of rewritten rows**: a rule's output goes back through
  dispatch instead of being appended, so cascades compose --
  `OBJECT_FORMAT 2` with a CONST rules feeder demotes to
  `OBJECT_FORMAT_CONST`, which with a VAR obj feeder fuses to
  `VAR_FORMAT_CONST` -- without any rule enumerating combinations.
  Termination discipline: every rewrite strictly reduces rows or moves
  to an op with no further rules.

**Phases stay distinct passes.**  The ordering constraints are
load-bearing (fusion gets first claim on feeders before run packing)
and today live only in docstrings; an explicit ordered pipeline makes
the contract structural. Passes remain independently callable so tests
keep targeting them individually. A few traversals of a
few-hundred-row list cost nothing.

## Invariants

- **POSITION is ethereal.**  The transparency set is exactly
  {POSITION}. Generalizing the lookback makes the `compress` peepholes
  POSITION-transparent too (today `squished[-1]` is literal, so
  `CONST x, POSITION, POP 1` fails to elide); each rule must be
  deliberate about re-emitting a skipped POSITION, since that is
  observable in tracebacks.
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
  owning meaning. Worked example from the format fusion: a quoted
  symbol as format rules means "its string" -- that coercion stays in
  the special, else a generic const-demotion would hand
  `PyObject_Format` a raw symbol. A pass must never need to know what
  a form *means*.

## Services

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
whose count drops to zero is itself removable, unblocking peepholes
and run packing that break at LABEL rows. Structured emission implies
a dead region contains balanced PUSH_*/POP_* pairs; the extent tracker
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

## The tail-call promotion pass

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
frame identity), so it lands like the format fusion did: mechanism
behind existing emission first, specials flipped separately, emission
tests moved deliberately.

## Simplifications this enables

Specials shed their emission-variant branching. `compile/format` is
the worked example: instead of four emission paths chosen by
`const_only_expr`, it emits one naive shape and the pass ladder does
const folding, CONST-rules demotion to `OBJECT_FORMAT_CONST`, and VAR
fusion. The same centralization retires per-special `const_only_expr`
duplication. (Where a fold is also a *diagnostic* -- all-constant
comparison chains raising on unorderable constants with the source
form in hand -- the special remains the right home; see the invariant
above.)

## Performance stance

The budget is bootstrap compile time at package import. Dict dispatch
should beat the elif chains outright; re-dispatch adds a bounded
constant. Verify with `tox -qe opgrams` (emission unchanged means the
refactor is behavior-preserving) and a bootstrap import timing before
and after.

## Migration order

1. Mechanical move: passes relocate from `compiler.py` into the new
   submodule under the processor class, behavior-preserving, tests
   untouched except imports.
2. Dispatch conversion: elif chains become the rule registry;
   generalized lookback replaces the literal `squished[-1]` peepholes;
   re-dispatch lands with its termination rule.
3. New services as their consumers arrive: label index with the
   tail-call pass; extent tracker with it; dead-region sweep and label
   GC last, if at all.

Sequencing: after the remaining fused-opcode TODO items, on its own
branch.
