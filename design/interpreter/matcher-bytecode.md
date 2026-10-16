# Matcher as bytecode

**Status:** implemented, 2026-10-14; the stages in section 12.6 record
the path. Sections 1 and 7 describe the engine this replaced as it
stood, and section 11 the proof of concept that became the
implementation.

**See also:** [../MATCHER.md](../MATCHER.md) (the pattern vocabulary,
the capture shapes), [../OPCODES.md](../OPCODES.md) section 9.2 (the
instruction reference) and section 1.8 (compression),
[../FLATTENING.md](../FLATTENING.md) (the predicate and extractor
entry this design closed), [resumable-reads.md](resumable-reads.md)
section 8 (the resume-row engine this design superseded),
[electors.md](electors.md),
[../archive/threaded-dispatch.md](../archive/threaded-dispatch.md),
[../language/define-matcher.md](../language/define-matcher.md).

## Summary

The matcher engine (`koskari/lib/matcher.c`, `matcher/`) is an
interpreter: a node array, a cursor, a backtracking log, subject
adapters, and a recursive walk on the C stack beside the evaluator.
Every acceleration it could take is one the evaluator already has.

This design lowers a pattern to evaluator instructions. One lowering
reads the frozen matcher tuple and emits an instruction list; the list
becomes a standalone `Code` called like any function, or is merged
inline into the function that contains a `match` form. Thirty-six
instruction shapes carry the matcher's tests, pulls and captures, fused
from the start on the pattern the `VAR_ITEM_*` family set. The node
implementations in `matcher/` survive as the library the handlers
call. The engine struct, the change log, the subject adapters,
`thaw.c`, the `KOS_CACHE_MATCHER` slot layout and the emission of
`MATCH_CASE` go.

A matcher is then a `KosCallable`: a Koskari caller reaches it by
`CALL`, and a predicate or extractor inside it is a further `CALL`
from its own frame. The demand path and the nested evaluator per
Koskari predicate are gone from every path that starts in Koskari.

## 1. The engine today

Facts the lowering reproduces, read from `matcher.c` and `matcher/`.

1. **Control is greedy and local.** `match_match` runs children in
   order and fails the whole sequence on the first failing child. It
   never re-enters an earlier sibling. `match_repeat` iterates while the
   inner node advances and never retries with fewer iterations. The
   choice points are `ONEOF` branches and `OPTIONAL`, and both commit
   to the first success. The validator guarantees at most one `REST`
   per branch with a deterministic suffix.
2. **The change log undoes the captures of a failed attempt.**
   `engine_snapshot` and `engine_rollback` record every slot write so a
   failed branch, `OPTIONAL`, `NOT` or `REPEAT` iteration leaves
   nothing behind. The set of slots a branch can write is known when
   the pattern lowers.
3. **Widths gate the walk.** Each node carries a static `min_width` and
   `max_width`; `match_match` and `match_group` recompute a `remaining`
   sum per execution and hand each child a `trailing_min`. The
   reservation changes an outcome only where a `REST` or `REPEAT` is
   followed by a non-empty suffix. Everywhere else a later sibling
   fails on its own nil test.
4. **Subjects are pre-walked.** `KosMatchSubject_FromKosPair` walks the
   spine and allocates two arrays per `FORM` descent;
   `FromPySequence` copies every list into a tuple. The runtime single
   subject avoids this at the root only.
5. **Tests with matcher-specific rules.** The literal test peels
   `Datum`, matches a gensym by its base, then falls to `Py_EQ`. The
   type test has a pure walk (`matcher_isinstance_pure`) that avoids
   `__instancecheck__` where the metaclass is `type`. A `CLASS` field
   treats `AttributeError` as a miss. A `MAP` entry probes a non-dict
   through `.get` with a private default. `PREDICATE` and `APP` call
   through `Koskari_DemandSync`.
6. **Captures from a pair spine are boxed** as `Datum` with the cell
   position, whether the spine is source or a runtime value under a
   sequence pattern (`tests/compile/specials/test_match.py`,
   `test_match_sequence_over_pair_binds_datum`). A tuple, list or
   single-value subject yields raw values. `GROUP` and the root yield a
   `DatumFrame` with `.source` and `.position`. A `REPEAT` iteration is
   a dict, a lone `Datum` for a capture-less single cell, a tuple of
   `Datum` for a multi-cell span. `syntax.py` depends on these shapes.
7. **Consumers.** The Python compiler (`Syntax.compile_impl` through
   `match_select` over every clause's matcher, the `@special`
   patterns, `match1` in `compile/helpers.py`), the reader layouts
   (`match1` in `reader/layouts/helpers.py` and `define_syntax.py`,
   before any compiler exists), the bootstrap macros, and `MATCH_CASE`
   at runtime, which binds through a cached `KosSlotMap` and leaves an
   unfired slot unbound.

## 2. Model

### 2.1 One lowering, two shapes

`MatchCompiler` and the frozen 6-tuple stay as the IR. The new step is
tuple to instruction list, in Python, with no dependency on
`Compiler`. It builds a `Code` the way `serialize/codex.py` does, so
the reader layouts and the Python-defined special patterns have it
before the compiler is up. The input is data, so the lowering ports to
Koskari under the tools migration.

From one instruction list:

- **standalone**: a `Function` whose one formal is `match-subject`,
  its externs the closure names of the namespace it is built in, a
  keyword extern stored into that namespace per call (section 2.7),
  returning the captures on a match and `None` on a miss; the
  `Matcher` surface calls it, and a `Syntax` dispatches through one
  such function over every arm, which returns the index and captures
  of the first arm that matches;
- **inline**: a fragment merged into the enclosing function by
  `compile/match`, the subject on the stack across the arm chain,
  captures bound as the arm's own locals, the clause bodies and guard
  lowering unchanged.

### 2.2 Pull and peek

A matcher instruction either **pulls** its element from a cursor the
walk keeps on the operand stack or **peeks** the element on top of the
stack. A pull instruction
consumes from the cursor and leaves nothing on the stack. A peek
instruction never pops. A miss never pops. A failure edge therefore
lands with the stack the clause entered with, and `DUP` does not occur
in a lowered pattern: the runtime clause works on the subject in place
(the keep flag of `MATCH_CASE` with no flag), and a `ONEOF` over one
element pulls once, has each branch peek, and pops at the join or on
the miss edge.

### 2.3 Cursors

A cursor is an entry on the operand stack, pushed by the gate that
opens its spine and named by every row that reads it by its depth
under the top (section 13). Over a pair spine the cursor is the cell
itself; a pull writes the cdr back at that depth and takes the car.
Over a list, tuple or `collections.abc.Sequence` the cursor is a small
native object holding the sequence and an index. `MATCH_FORM` decides
which at descent from the PEP 634 gate, and every pull instruction
reads either. No spine is pre-walked and no sequence is copied.

The pull instructions are one shape: cursor read at its depth, the
advanced cursor written back at that depth, element delivered, miss
jumps with the cursor untouched. A sequence cursor is written back in
place when its entry is its only holder and replaced by a new cursor
when a mark or a cloned segment holds it, so a copy of the entry is a
saved position for either kind. A step that must read ahead without
moving, the zero-width `NOT`, uses `MATCH_PEEK`.

### 2.4 Elements and boxing

The boxing rule of fact 6 is kept: an element pulled from a pair
cursor is a source cell and binds as `Datum(car, position)`; an
element pulled from a sequence cursor binds raw; the runtime root
subject binds raw. A pull fused with a bind knows its cursor and boxes
accordingly. The unfused `MATCH_NEXT` pushes the boxed `Datum` for a
pair cursor and the value for a sequence cursor, so the peek tests
behind it read one shape, peeling `Datum` as `engine_test` does, and
`MATCH_BIND` stores what it finds.

### 2.5 Failure edges replace the change log

Every node has a failure label: the next branch of the nearest
`ONEOF`, the continuation of an `OPTIONAL`, the exit of a `REPEAT`
loop, the inversion of a `NOT`, or the clause's own miss. The slots a
failed attempt may have written are known, so the edge emits one
`FAST_CLEAR` per slot (`KosNamespace_FastClear` tolerates an unbound
slot). A choice point whose retry needs the cursor saves it into one
more scratch slot and restores it on the edge. No snapshot, no rollback
record, no heap growth.

The runtime inline shape stores each capture into its declared name
as it fires, so a clause that fails after a capture leaves that name
bound. PEP 634 leaves the bindings of a failed match unspecified, and
user code may rely neither on their presence nor on their absence;
CPython leaves them in place as well. The engine's rollback was
stricter than the contract, and the scratch slot and commit move per
capture it would cost here buy nothing the contract promises.

### 2.6 Widths

Static per node as today, computed by the lowering from the same
rules. A `remaining` count is emitted only in a spine where a `REST`
or `REPEAT` has a non-empty suffix; `MATCH_FORM` carries the inner
pattern's min and max for the PEP 634 length precheck on sequences;
`MATCH_REST` validates properness and the minimum tail where a `REST`
exists; `MATCH_END` rejects an improper or overlong spine everywhere
else. The per-execution `remaining` arithmetic of `match_match` is
gone.

### 2.7 Externs

The extern table becomes operands. Every extern is a name: a type, a
predicate, an extractor, a mapping key. In the inline shape each is a
`VAR` field requested through `request_var`, as `compile/match` does
today for the missing externs, so the closure apparatus brings the
module binding into the function namespace at build and `convert_fast`
gives it a fast slot. In the standalone shape each is a keyword formal
(superseded: a closure name over the matcher's namespace, section
12.2). `CONST` fields
carry values only: the literal of a literal test, a mapping key written
as an atom, a field name. The engine's missing-binding `TypeError`
becomes the formals machinery's arity error; `missing_externs` is the
formals spec read back.

Boundary shift: there is no defaults table. `KosMatcher_ExternDefaults`,
`reset_extern_defaults`, the defaults version watcher, and the per-site
env cache have no successor. A user type's name is the ref the closure
captured: a store into that ref reaches every site at once, a bind of a
new ref to the name reaches none of them, as for any closure.

Core types are not externs at all. `pair`, `symbol`, `keyword`, `int`,
`str`, `tuple` and `list` lower to the `MATCH_CORE_TYPE` shapes, whose
RAW field carries a `KosCoreType` ordinal and whose test is the type
walk the builtin's isinstance would run, with no reference to resolve.
A pattern's `int` is therefore syntax, as master's defaults table made
it: a module that binds `int` to something else, before or after the
pattern is compiled, changes nothing the pattern tests. This closes the
implicit-extern defect the 2026-10-14 review found, where every
`syntax-rules` head resolved `symbol` in the defining module, and the
hygiene collision of a macro's `int` with a user sub-pattern's `int`.
The decision is `MatchCompiler.lower`'s, so the frozen tuple's extern
table lists only real externs. CONST never holds a type.

### 2.8 Captures and repeat iterations

`MATCH_CAPTURES` builds the captures from a table of slot name and
fast index pairs, skipping an unbound slot, so the sparse captures dict
and an unfired `OPTIONAL` need no sentinel. On the syntax side it pops
the source cell for `.source` and `.position` and builds a
`DatumFrame`; on the runtime side it builds a dict. A `REPEAT`
iteration ends in `MATCH_ITERATION`, which builds the iteration's
captures from its slots, or the boxed span when none fired, clears the
slots, and appends the value to the items list under its mark. A
capture-less iteration appends the boxed element (`MATCH_NEXT_APPEND`),
and an `x ...` at the end of a spine is one `MATCH_COLLECT`.

### 2.9 Calls

A `PREDICATE` is `CALL 1` then `JUMP_FALSE`; an `APP` is `CALL 1` then
the sub-pattern on the result. A Koskari predicate installs in the
matcher's own frame and flattens; an awaiting predicate inside an
async function awaits in place. This closes the "runtime match
predicates and APP extractors" entry of `FLATTENING.md` and supersedes
section 8 of `resumable-reads.md`, whose precondition (the walk's
position moved off the C stack) is the frame pc.

## 3. Instruction set

Every instruction is an EXTRA row within the four-field schema. `L` is
the miss label. `cur`, `mark` and `list` name entries on the operand
stack by their depth under the top, RAW fields the lowering computes
(section 13); `slot` names a capture slot, a VAR field `convert_fast`
lowers to the row's FAST twin where the name has a fast slot, as it
does for `ITER_NEXT_STORE_VAR` and the item family. At module scope
and past the fast-slot cap the VAR form runs and stores by name, as
`VAR_STORE` does. `type` is a VAR field (section 2.7), lowered with
the rest of the row, except for a core type, which the
`MATCH_CORE_TYPE` shapes carry as a RAW ordinal. `key` is a CONST for an atom key and a VAR
for a symbol key; `const` is a value. `widths` packs min and max into
one RAW field, eight bits each, `0xFF` unbounded, since a row immediate is
sixteen bits; a FORM's widths saturate safely, and a REST whose minimum
tail or fixed suffix exceeds 255 is refused at validation.

| Instruction | Fields | Kind | Effect |
|---|---|---|---|
| MATCH_NEXT | L, cur | pull | push the element (section 2.4) |
| MATCH_PEEK | L, cur | pull | push the next element without advancing; what a zero-width NOT reads |
| MATCH_AVAILABLE | L, cur, needed | pull | at least `needed` elements remain: the reservation a suffix makes against a repeat, an optional or a branch |
| MATCH_ITEMS | | push | an empty list, a repeat's items, pushed under the pass marks |
| MATCH_COUNT | L, list, n | none | the list holds at least n items: a repeat's minimum |
| MATCH_NEXT_BIND | L, cur, slot | pull | VALUE, BIND, a bare rule variable |
| MATCH_NEXT_LITERAL | L, cur, const | pull | a keyword or literal element |
| MATCH_NEXT_TYPE | L, cur, type | pull | anonymous `(:: T)` |
| MATCH_NEXT_TYPE_BIND | L, cur, type, slot | pull | `(x :: T)` in a spine |
| MATCH_NEXT_CORE_TYPE | L, cur, core | pull | anonymous `(:: int)` on a core type, the ordinal in a RAW field |
| MATCH_NEXT_CORE_TYPE_BIND | L, cur, core, slot | pull | SYMBOL, CALLEDBY, PAIR, KEYWORD, `(x :: int)` in a spine |
| MATCH_NEXT_SAME | L, cur, slot | pull | a repeated rule variable |
| MATCH_MARK | cur | push | the cursor pushed as its position |
| MATCH_RESET | cur, mark | none | the cursor moved back to the position at `mark` |
| MATCH_NEXT_FORM | L, cur, widths | pull | descend into the next element; the inner cursor pushed |
| MATCH_NEXT_APPEND | L, cur, list | pull | box one element onto the list at `list` |
| MATCH_COLLECT | L, cur, list | pull | every remaining element boxed onto the list at `list`; nil required |
| MATCH_REST_BIND | L, cur, min, slot | pull | BODY, `*: r`, a dotted tail |
| MATCH_REST | L, cur, min | pull | anonymous tail |
| MATCH_END | L, cur, pop | pull | the cursor must be at its end; with `pop` set it is popped, its extent ending here |
| MATCH_LITERAL | L, const | peek | literal test |
| MATCH_TYPE | L, type | peek | type test |
| MATCH_TYPE_BIND | L, type, slot | peek | the runtime root `(x :: T)`; a typed branch |
| MATCH_CORE_TYPE | L, core | peek | core type test |
| MATCH_CORE_TYPE_BIND | L, core, slot | peek | the root `(x :: int)` on a core type |
| MATCH_SAME | L, slot | peek | equality with a bound slot |
| MATCH_FORM | L, widths | peek | the runtime root sequence pattern; the cursor pushed |
| MATCH_CLASS | L, type, positional count | peek | isinstance, and `__match_args__` long enough for the count |
| MATCH_FIELD_POS | L, type, position | peek | push the positional field named by `__match_args__` for a sub-pattern |
| MATCH_FIELD_POS_BIND | L, type, position, slot | peek | a positional field captured directly |
| MATCH_FIELD_KW | L, name | peek | push the attribute for a sub-pattern |
| MATCH_FIELD_KW_BIND | L, name, slot | peek | a keyword field captured directly |
| MATCH_MAP | L | peek | the mapping gate |
| MATCH_KEY | L, key | peek | push the value for a sub-pattern; CONST and VAR key forms |
| MATCH_KEY_BIND | L, key, slot | peek | an entry captured directly; CONST and VAR key forms |
| MATCH_BIND | slot | peek | store the element, nothing popped |
| MATCH_CAPTURES | names, base, start, cur | push | the captures of a scope: the names tuple, the first of the scope's run of capture slots, the start and the cursor by depth; a `DatumFrame` with the span as its source over a pair spine, a dict otherwise; a NAMED form reads the captures by name for the inline target |
| MATCH_ITERATION | names, base, cur, mode | push or append | one repeat iteration's value, its mark on the top and its items under that: the captures when any slot fired, else the boxed span; mode 1 appends it to the items, mode 0 pushes it; clears the slots it read; the same NAMED form |

Thirty-eight shapes. Every shape that names a capture slot or a type
has a VAR and a FAST entry in `opcodes.incl`, on the precedent of the
`VAR_*` and `FAST_*` families; the shapes whose only slot was a cursor
keep both entries with one schema; the two key shapes have a CONST key
and a VAR key form besides; `MATCH_CAPTURES` and `MATCH_ITERATION` have
the slot-run form the standalone target emits and a NAMED pair the
inline target emits, whose CONST is the (keys, locals) tuple.
Seventy-five opcodes as built, beside the retired `MATCH_CASE`. The FAST entry is the one that runs in a function body.

Handler notes:

- **Miss semantics.** A pull miss leaves the cursor untouched and
  pushes nothing. A peek miss leaves the stack untouched. The lowering
  knows the live entries at every miss site, cursors and marks with
  the elements, and emits the `POP` count on the edge where one is
  needed.
- **Pair cursor.** A pull on a non-nil pair writes the cdr at the
  cursor's depth and works on the car; on nil or a non-pair it misses.
  A `MATCH_END` on nil succeeds; on anything else it misses, the
  improper-spine miss.
- **Sequence cursor.** The cursor object holds the sequence, its
  length and the index, untracked and opaque to Python. A pull past
  the length misses. An advance moves the cursor in place when its
  entry is its only holder, else stores a new one at the entry; a
  freelist of eight carries the replacements. `MATCH_REST`
  slices with `PySequence_GetSlice`, producing the tuple
  `wrap_slice_datums` produces today. A `collections.abc.Sequence`
  that is not a list or tuple is read through `PyObject_GetItem`, once
  per element, never iterated.
- **MATCH_FORM.** The gate from `match_form`: `str`, `bytes`,
  `bytearray` miss; mappings, sets and iterators miss; a pair or nil
  takes the pair cursor; a list, tuple or `Py_TPFLAGS_SEQUENCE` type
  (the ABC on 3.9) takes the sequence cursor and the length precheck
  against `widths`. The element is peeled first.
- **MATCH_TYPE family.** The type read from its slot, then
  `matcher_isinstance` as the body. The pure walk is an elector body
  if the benchmark earns it; the cache column stays zero at first.
- **MATCH_LITERAL family.** `engine_test`'s literal case: peel,
  identity, gensym base, `Py_EQ`.
- **MATCH_SAME family.** Peel both, a `DatumFrame` operand misses,
  identity, `Py_EQ`.
- **MATCH_CLASS.** isinstance through the box, then `__match_args__`
  checked against the positional count; the 3.9 synthesis stays behind
  its version gate; no cache slot. `MATCH_FIELD_POS`
  reads the positional name from `__match_args__` and `MATCH_FIELD_KW`
  the attribute; a missing attribute is a miss.
- **MATCH_MAP, MATCH_KEY family.** `matcher_mapping_gate` and
  `map_probe` as the bodies.
- **MATCH_CAPTURES, MATCH_ITERATION.** The names tuple is a CONST and
  the slot run starts at the first capture slot; the NAMED forms carry
  the (keys, locals) pair. A captures row names its start and cursor
  by depth; an iteration runs with its mark on the top and its items
  under that, so it names the cursor alone and carries a mode. Over a
  pair spine the result is a `DatumFrame` whose source is the span and
  whose position is the start cell's.
- **Naming.** Suffix composition as the item family uses: the pull,
  the test, the bind, in that order. One field kind is added: RAW, an
  immediate the instruction interprets its own way, where COUNT would
  have claimed a stack amount.

## 4. Lowering by node kind

| Node | Instructions |
|---|---|
| MATCH | the children in order; no instruction of its own |
| TEST_ANY | MATCH_NEXT then `POP 1`, or nothing when the element is captured |
| TEST literal | MATCH_NEXT_LITERAL; MATCH_LITERAL after a bare pull |
| TEST type | MATCH_NEXT_TYPE or MATCH_NEXT_TYPE_BIND under a CAPTURE; the peek forms after a bare pull |
| TEST predicate | MATCH_NEXT, `CALL 1` on the extern, `JUMP_FALSE` to the edge, `POP 1` |
| SAME | MATCH_NEXT_SAME; MATCH_SAME after a bare pull; the slot resolved statically, so depth one names the enclosing group's slot and a deeper depth is an unconditional miss |
| CAPTURE | the fused `_BIND` form of its sub-node; MATCH_BIND after a bare pull; over a REPEAT the list, over a GROUP the captures; over a wider node MATCH_MARK, the node, and the span as MATCH_ITERATION of an empty table |
| FORM | MATCH_NEXT_FORM pushing the inner cursor, the inner children, MATCH_END on it with the pop flag |
| GROUP | MATCH_MARK, the children into the group's slots, MATCH_CAPTURES over the group's table; stored into the capturing slot or popped; the mark popped, unless the group took the fresh mark of the repeat around it |
| ONEOF | branches all one element wide with no reservation: one MATCH_NEXT, each branch peeks, a branch's miss edge clears its slots and jumps to the next branch, the join pops; otherwise MATCH_MARK, each branch guarded by MATCH_AVAILABLE and pulled, its miss edge MATCH_RESET and clears, the last branch's miss edge the enclosing edge |
| OPTIONAL | MATCH_AVAILABLE against the reservation, MATCH_MARK, the inner node pulled, the mark popped; its miss edge MATCH_RESET, clears, the mark popped |
| REPEAT | MATCH_ITEMS when anything keeps the passes; a loop of MATCH_AVAILABLE against the reservation, MATCH_MARK, the inner node, MATCH_ITERATION appending to the items, the mark popped, `JUMP` back; the pass's miss edge MATCH_RESET, clears, the mark popped; a `min_count` is MATCH_COUNT after the loop; the items go into the capturing slot by `VAR_STORE` or are popped |
| REST | MATCH_REST_BIND or MATCH_REST; with a non-empty suffix, the `remaining` count feeds the min |
| NOT | MATCH_PEEK, the inner node as peek instructions with its hit edge and miss edge exchanged, `POP 1`; captures are forbidden inside NOT already |
| CLASS, FIELD | MATCH_CLASS, then per field MATCH_FIELD_POS_BIND or MATCH_FIELD_KW_BIND for a captured field, or MATCH_FIELD_POS or MATCH_FIELD_KW, the sub-pattern, `POP 1` |
| MAP, MAP_ENTRY | MATCH_MAP, then MATCH_KEY_BIND or MATCH_KEY, the sub-pattern, `POP 1` |
| APP | MATCH_NEXT, `CALL 1` on the extern, the sub-pattern as peek instructions, `POP 1` |

## 5. Examples

`(define x 1)` against `MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr"))`,
standalone. The subject is fast 0, the captures fast 1 to 3; `symbol`
is a core type and no slot. The gate pushes the cursor above the
subject, every pull names it at depth 0, and the captures read the
subject at depth 1 and the cursor at depth 0.

```
 0  FAST                           0 (subject)
 1  MATCH_FORM_FAST                -> 11, widths (3 . 3)
 2  MATCH_NEXT_CORE_TYPE_BIND_FAST -> 8, cur 0, symbol, 1 (called-by)
 3  MATCH_NEXT_CORE_TYPE_BIND_FAST -> 8, cur 0, symbol, 2 (sym)
 4  MATCH_NEXT_BIND_FAST           -> 8, cur 0, 3 (expr)
 5  MATCH_END_FAST                 -> 8, cur 0, keep
 6  MATCH_CAPTURES                 names, first 1, start 1, cur 0
 7  RETURN 1
 8  POP 1 ; JUMP 11
11  CONST None ; RETURN 1
```

The miss edge at 8 pops the cursor; the frame takes the subject.

`(match v [[#[a b *: r]] a] [[(x :: int)] when: (> x 3) x] [else: None])`,
inline. The subject stays on the stack across the arms; the first
arm's gate pushes its cursor and its end pops it, so the hit path
pops the subject alone.

```
 0  FAST                  0 (v)
 1  MATCH_FORM_FAST       -> 7, widths (2 . unbounded)
 2  MATCH_NEXT_BIND_FAST  -> 6, cur 0, 1 (a)
 3  MATCH_NEXT_BIND_FAST  -> 6, cur 0, 2 (b)
 4  MATCH_REST_BIND_FAST  -> 6, cur 0, counts, 3 (r)
 5  MATCH_END_FAST        -> 6, cur 0, pop
 .  POP 1 ; FAST 1 ; JUMP out
 6  POP 1 ; JUMP 7
 7  MATCH_CORE_TYPE_BIND_FAST -> 9, int, 4 (x)
 .  CHECK_COMPARE_FAST  GT, fast 4, c#3, -> 9
 .  POP 1 ; FAST 4 ; JUMP out
 9  POP 1 ; CONST None
out RETURN 1
```

`(my-or e ...)`, a `syntax-rules` pattern, which lowers to `CALLEDBY`
then `repeat_capture("e", TEST_ANY())`: the items pushed above the
cursor, each pass marking, pulling, appending the span and popping
its mark, the loop's miss edge resetting and popping, the items stored
into the capture slot.

```
 0  FAST 0
 1  MATCH_FORM_FAST                -> 18, widths (1 . unbounded)
 2  MATCH_NEXT_CORE_TYPE_BIND_FAST -> 16, cur 0, symbol, 1 (called-by)
 3  MATCH_ITEMS
 4  MATCH_MARK_FAST                cur 1
 5  MATCH_NEXT_FAST                -> 10, cur 2
 6  POP 1
 7  MATCH_ITERATION                (), first 0, cur 2, append
 8  POP 1 ; JUMP 4
10  MATCH_RESET_FAST               cur 2, mark 0
11  POP 1 ; FAST_STORE 2 (e)
12  MATCH_END_FAST                 -> 16, cur 0, keep
13  MATCH_CAPTURES                 names, first 1, start 1, cur 0
14  RETURN 1
16  POP 1 ; JUMP 18
18  CONST None ; RETURN 1
```

`[x 1] ...` under `let` is the same loop over a form: `MATCH_NEXT_FORM`
pushing the inner cursor, `MATCH_NEXT_TYPE_BIND` and `MATCH_NEXT_BIND`
into the group's slots at depth 1 under it, `MATCH_END` popping it,
`MATCH_ITERATION` appending the pass. The `(expr as: name)`
alternative of `BINDINGS` is where `MATCH_NEXT`, the peek instructions
and `MATCH_BIND` appear: one pulled element tried by each branch and
popped on the miss edge.

## 6. Consumers

- **`Syntax`.** All clauses lower into one standalone `Code`; clause
  k's miss edge is clause k+1's entry; the function returns `(values
  index captures)`, None when no clause matches. A clause whose
  matcher is a frozen tuple lowers inline; a clause a module compiled
  to a function of its own is called from the `Code`, its result
  tested against None, so a composed syntax (`+`, `extend-syntax`)
  has one function over both halves and an arm keeps a matcher of its
  own. One evaluator entry per form compiled instead of one per clause
  tried. The Python `Syntax.compile_impl` indexes the values object; a
  self-hosted one uses the values binding forms. Once clauses are
  instructions, the pure tests (literal, type, nil, length) can be
  hoisted into a shared ladder across clauses; predicate, extractor
  and field reads cannot, since they run user code in an observable
  order.
- **`compile/match`.** Emits the inline shape in place of the extern
  loads and `MATCH_CASE`; declares the capture names and the scratch
  temporaries; clause bodies, guards and `else:` unchanged.
- **Python entry.** `Matcher.match(subject, /, **externs)`, `match1`
  and the reader layouts call the standalone function through the
  demand wrapper, which adapts a Python call into the demand entry:
  `KosContext_New`, `koskari_eval`, `Py_EnterRecursiveCall` per call.
  Nothing matcher-specific crosses the boundary. The entry disappears
  from the compile path when the compiler self-hosts.
- **Bootstrap order.** The lowering is Python with no `Compiler`
  dependency, so it is available at the point `koskari.matcher` is
  today: before the reader layouts and before the first `.ki` loads.
- **`.kio`.** A standalone matcher is a `FUNCTION_SPEC` constant; an
  inline one is instructions. A frozen tuple stored as a constant is
  plain tuple pool entries.
- **`define-matcher`.** A template expands at a use site as designed;
  the result lowers like any pattern. A matcher bound as a value is
  the standalone function.

## 7. What it removes

- `MatchEngine`, `DictChange`, `engine_snapshot`, `engine_rollback`,
  `engine_discard_pending_change_refs`, `engine_changes_to_dict`.
- `KosMatchSubject` and its three adapters; the two arrays per pair
  descent and the tuple copy per sequence descent.
- `match_node`, `match_node_kind`, the recursive walk and its
  `Py_EnterRecursiveCall` per level.
- `thaw.c`, `KosMatcher_FromTuple`, `KosMatcher`'s node array and the
  builder, `validate.c` as a runtime step (the lowering validates).
- `KOS_CACHE_MATCHER`, `KosSlotMap` use at a match site, the env
  cache, the root pre-test, `KosMatcher_MatchSelect`,
  `KosMatcher_RunSlotsVectorcall`.
- `KosMatcher_ExternDefaults`, `KosMatcher_ResetExternDefaults`, the
  defaults version watcher.
- `Koskari_DemandSync` from `matcher_call_extern`.
- The emission of `MATCH_CASE`. The opcode and its handler stay in the
  table.

What stays, as the library behind the handlers: `matcher_isinstance`
and the pure walk, the literal test, `match_form`'s gate and the
sequence and mapping gates, `match_one_field` and the 3.9
`__match_args__` synthesis, `map_probe`, `kos_datum_new`,
`wrap_slice_datums`, `datum_frame_set_source`, `Datum`, `DatumFrame`,
`gensym`.

## 8. Contracts kept and changed

Kept:

- First clause wins; captures persist on guard failure; no clause and
  no `else:` yields `None`.
- The capture shapes of fact 6, including the boxing of a runtime pair
  under a sequence pattern.
- PEP 634 gates and length precheck; strings are atoms; an iterator is
  never consumed; an improper spine is a miss.
- The gensym literal rule; `SAME` against a `DatumFrame` misses.
- A raising predicate, extractor or field read propagates; a missing
  attribute or key is a miss.
- `MATCHER.md`'s pattern vocabulary and the three dialect parsers.

Changed:

- There is no extern defaults table; `int` in a pattern is the ref the
  closure captured, written through by a store and left behind by a
  bind (section 2.7).
- A Koskari predicate or extractor flattens; an awaiting one awaits in
  place rather than raising `KoskariSyncViolation` (section 2.9).
- A clause that fails after a capture may leave the name bound, as
  PEP 634 permits; the engine rolled it back (decided 2026-10-14).
- `MATCH_CASE` is no longer emitted; corpus records with a `compile`
  phase over `match` forms change.
- The `Matcher` type, `MatchResult`, `missing_externs`,
  `get_extern_defaults` and `reset_extern_defaults` are decisions in
  section 10.

## 9. Performance

What to expect is not known; what moves is.

Where the cost moves per element: the engine does a recursion level,
a width subtraction, an adapter indirection and a change record; the
instruction does one dispatch, a pair read and a store. The threaded
dispatch makes the instruction the cheaper unit on every count that is
not an allocation.

Allocations per match: the engine allocates two arrays per pair
descent, a tuple per sequence descent, a change stack past eight
records, a slot array past eight slots, a `MatchResult`; the lowered
code allocates the same `Datum` and captures objects and one cursor
object per sequence descent. Fewer, never more.

Per site at runtime: no thaw, no slot map, no env resolution, no root
pre-test machinery, no handler prologue classifying the env; the
subject is tested where it stands.

The Python entry: an engine call from Python is a direct C call; a
standalone matcher called from Python is a context and an evaluator
entry. This is the one place the design can lose, and it is paid only
while the compiler and reader are Python.

Code size: a `MATCH_CASE` site is one instruction and one frozen tuple
constant; the lowered clause is one instruction and one 32-byte row
per element. A `Syntax` with many clauses grows accordingly; the
shared ladder of section 6 is the counterweight.

Measurement, in this order, with the baseline recorded before the
first edit:

1. `tools/matchbench.ki` `match-*` cells (the inline shape against
   `MATCH_CASE`), per clause shape: sequence hit, sequence miss, class,
   map, predicate, guarded, externs.
2. `engine-*` cells against the standalone shape called from Koskari,
   where no boundary is crossed.
3. `engine-*` cells against the standalone shape called from Python,
   which is the compile-time path, with and without the host cache.
4. Wall time of `make test` phase 3 and 4, which is dominated by
   compile-time matching.

The gates: 1 and 2 must win; 3 must not lose by more than the compile
path can absorb, which 4 measures.

## 10. Open decisions

1. Decided 2026-10-14: `Matcher` and `MatchResult` are Python classes
   in `koskari.matcher` over the function, `from_tuple` builds one
   from the frozen tuple, and a `Syntax` dispatches through one
   function over every arm (section 6).
2. Decided 2026-10-13: every extern, `int` and `str` included, is a
   `VAR` field requested through `request_var`, so the closure
   apparatus brings the module binding into the function namespace at
   build. `CONST` carries values only. There is no defaults table.
3. Decided 2026-10-14: direct stores; a failed clause may leave a
   capture bound, as PEP 634 permits. Decided 2026-10-15: a capture
   name is the arm's own local, so a binding of that name made before
   the match is overwritten by any attempt at it, and an `(or ...)`
   alternative that misses leaves its names unbound; its miss edge
   clears them so a failed branch is absent from the captures the
   standalone function and a repeat iteration gather. PEP 634 cannot
   reach the case, every alternative there binding the same names.
4. Which conditional reads start as instructions and which as builtin
   calls; the table in section 3 is the full set.
5. Whether the `MATCH_TYPE` family takes an elector slot from the
   start.

## 11. Proof of concept

Scope, in this worktree, in order. `compile/match` never emits
`MATCH_CASE` again from the first step: the new path is the only path,
and a pattern the step does not yet lower is a compile error naming
the gap.

1. The runtime inline shape for the whole case dialect the tests
   exercise: literal, bare capture, `x :: T` with `T` a default or a
   module binding, `#[...]` and `#(...)` nested to any depth, `*: r`,
   `(REST r)`, `Point[x y]` with positional and keyword fields,
   `#{K: PAT}`, `(app f PAT)`, `(? pred)`, `(or ...)`, `(not ...)`,
   guards and `else:`. Shapes: MATCH_FORM, MATCH_NEXT_FORM,
   MATCH_NEXT, MATCH_NEXT_BIND, MATCH_NEXT_TYPE, MATCH_NEXT_TYPE_BIND,
   MATCH_NEXT_LITERAL, MATCH_REST, MATCH_REST_BIND, MATCH_END,
   MATCH_LITERAL, MATCH_TYPE, MATCH_TYPE_BIND, MATCH_BIND,
   MATCH_CLASS, MATCH_FIELD_POS, MATCH_FIELD_POS_BIND, MATCH_FIELD_KW,
   MATCH_FIELD_KW_BIND, MATCH_MAP, MATCH_KEY,
   MATCH_KEY_BIND, with their VAR and FAST entries and the two key
   forms of the item shapes. Captures store directly into the declared
   names (decision 3). `SAME`, `REPEAT` and `GROUP`
   reached through registry passthrough are the named gap until step
   2. Measured by gate 1; `tests/compile/specials/test_match.py` is the
   contract check.
2. The standalone shape over source for the special-form vocabulary
   `CALLEDBY`, `SYMBOL`, `VALUE`, `BODY`, `LITERAL_KW`, `REST`,
   `FORM`, `ONEOF`, `REPEAT`, `GROUP`, `SAME`: MATCH_NEXT_SAME,
   MATCH_SAME, MATCH_NEXT_APPEND, MATCH_COLLECT, MATCH_CAPTURES,
   MATCH_ITERATION, source boxing, the cursor save and restore of a
   repeat. One special wired through it beside the engine; the step 1
   gap closes. Measured by gates 2 and 3.
3. `BINDINGS` and `FORMALS`; `Syntax` as one dispatcher; the reader
   layouts and `match1`; gate 4.

Out of scope for the proof: the `.kio` format bump beyond the opcode
MINOR, the shared ladder across clauses, the `Matcher` surface
decision, removal of any engine code.

## 12. The matcher compiler

The matcher is a second compiler beside the standard one. Its source
is the matcher DSL, the forms the registry names and the `Pattern`
builders construct; its output was a matcher object and is now
bytecode. As with the standard compiler, the forms are dynamic: a new
form that lowers to existing ones is registered, by us or by a user,
and hides its complexity in the DSL without touching the compiler.

### 12.1 Parts

- **The registry**, `MATCHER_REGISTRY`, is the default table of
  surface heads to form factories, extended by `register_matcher` and
  later by a module-scoped layer `define-matcher` adds. Unchanged.
- **The forms** are the `Pattern` classes. The primitives are the
  closed set the compiler lowers; a composite expands to primitives in
  `_expand`. A new composite is a new class, or a registry factory that
  builds existing forms; neither touches the compiler. `define-matcher`
  is the Koskari spelling of the same thing.
- **The frozen tuple** stays as a form's lowered value: serializable,
  what `Pattern.freeze`, `thaw` and `Matcher.from_tuple` exchange, and
  the input of code emission. Nothing stores it.
- **The compiler**, `MatchCompiler` grown to its name: forms to the
  tuple, as today, and the tuple to instructions through one handler
  per primitive kind, with two targets. The inline target emits into a
  fragment of the standard compiler and leaves the names to
  `convert_fast`; the standalone target allocates its own labels and
  fast slots and builds `Code`, `FunctionSpec` and a `Function` with
  nothing but `koskari.lib`, so it exists before the standard compiler
  does. The handlers are the emitter of `match_code.py`, which moves
  to `koskari/match_code.py` below both `koskari.matcher` and
  `koskari.compile`.
- **There are no default externs.** The seven builtin type names are
  core types, tested by ordinal with no reference; every other extern of
  a matcher built outside a module is a keyword of `Matcher.match`, given
  on every call. The defaults table and its version counter went with
  the core type instructions.

### 12.2 Environments

A standalone matcher is a `Function` over a namespace, and its externs
are variables in that namespace, reached by the closure apparatus as
`match` reaches them. The compilation decides the namespace:

- a matcher built from Python gets an empty base namespace of its own,
  which each `Matcher.match` call fills from its keyword externs; a core
  type is not an extern and needs none;
- a matcher built in a module closes over the module's namespace, so a
  type or predicate the pattern names is the module's binding.

A store into a namespace reaches every matcher closed over it; a bind
of a new ref reaches none, as for any closure. `Matcher.match(subject,
**externs)` stores its keyword arguments into the matcher's own base
namespace before the call.

### 12.3 The three lifetimes

1. **The Python specials' matchers.** Born when `koskari.compile.
   specials` imports, and the parsers' meta-matchers when
   `koskari.matcher` imports: `Pattern` objects compiled through the
   standalone target over the defaults namespace, one function each
   for the life of the process, entered from the Python compiler at
   every compile of their form through `KosFunction_VectorCall` with
   the host-cached context. They never serialize and never see a
   module. This is the path that kickstarts the compiler.
2. **`syntax-rules` and `syntax-match` macros.** The pattern is source
   in a `define-syntax` form. The defining module's compile lowers it
   to a lambda, a `FunctionSpec` constant in the module's code, and the
   form lowers to `(build-syntax "name" <lambda> <names> (quote-syntax
   TEMPLATE) ...)`, `<names>` carrying the slot names and the rule
   dialect's repeat variables. At module load `BUILD_FUNCTION` closes
   over the module as for any lambda; the `.kio` holds bytecode and
   load parses nothing. At a use site the compiler calls the function
   with the use form and the captures feed the template or the handler
   as the `MatchResult` captures do now. Marked atoms in a pattern a
   template wrote resolve at the defining compile through
   `extern_atoms` and the candidates, as `match` resolves them. The
   `build-*` builtins keep accepting a `Pattern`, compiled in the live
   namespace handed to them, for the REPL and for transformers built
   by hand; both entries build the same `Transform`, which keeps
   `pattern_src` for display.
3. **`match` and `case`.** Inline: the clause instructions interleave
   with the bodies and guards, captures are the enclosing function's
   locals, externs its closure names, cursors and marks entries on its
   operand stack for the walk. Nothing has a lifetime of its own.

Every matcher is instructions; the cases differ in container and
environment. Two of the three are the standard compiler lowering a
pattern it finds in source; the first is the one build that must stand
without it.

### 12.4 The captures epilogue

During a match the captures are fast slots and nothing else, one run
of them after the formals in the standalone target. `MATCH_CAPTURES`
packs them: its row is the names tuple (CONST, the slot table
`Matcher.slots` exposes), the fast index of the first capture slot
(FASTVAR, name `i` at `base + i`), and the depths of the start
position, the subject for the root and a mark for a group, and of the
cursor. It skips an empty slot, so an unfired `OPTIONAL` or the other
branch of a `ONEOF` is absent; a pair cell as the start makes the
result a `DatumFrame` whose `.source` is the span from the start cell
to the cursor and whose `.position` is the start cell's, an index as
the start makes it a plain dict. The root and a named `GROUP` are the
same instruction over different start and cursor depths;
`MATCH_ITERATION` is the same read of an iteration's slots, the boxed
span when none fired, with the slots cleared, appended to the items
under its mark. The NAMED twins read each capture by name.

### 12.5 Weighed and set aside

- **Every matcher a lambda, `match` included**, with by-ref formals
  carrying the captures into the caller's slots. Measured 2026-10-14:
  a flattened call with one by-ref store costs 119 ns against 4 ns for
  the inline store, and the step 1 clause costs 5 to 25 ns, so eight
  clauses as calls would cost about 950 ns against the engine's 280
  and the inline 95. `match` stays inline. With that, by-ref formals
  have no role in the matcher: the standalone matcher's callers are
  the compiler and the transformers, which want the captures dict the
  epilogue returns.
- **A dynamic node per match** (`PUSH_MATCH` / `POP_MATCH`) holding
  cursors, marks and a stack mark for misses. Everything it would hold
  is an entry on the operand stack or compile-time knowledge, and the
  segment is already what a continuation copies; the push and pop
  would cost about 10 ns on every attempt's hit path for nothing the
  stack does not give (section 13).
- **A namespace around a form's instructions** to keep the cursor
  temporaries out of the enclosing scope. Built and measured on
  2026-10-14 in `compile/specials/match.py`: the subject bound into
  the form's own namespace, every arm reading it back, the matched
  arm's index delivered through the one pop and dispatched to the
  bodies outside. The push is about 60 ns; the cost is the
  write-through closure cell per capture per entry, and the churn
  match_case kernel went from 0.21 s to 0.97 s.
- **Unpacking the FORM and REST widths** and the wide-pattern
  fallbacks. Held.

### 12.6 Stages

Stage 1 completed 2026-10-14. `koskari/match_code.py` holds both
targets; sixty-six opcodes; `tests/matcher/test_match_function.py`
and `tests/corpus/test_match_function_oracle.py` run the standalone
function beside the engine, the oracle over 8889 forms of the corpus,
6138 comparisons against the 312 matchers of the specials their heads
name, 2288 of them matches, every capture equal. Timings in ns per
match, the function called from Koskari against `Matcher.match`
(probe scripts in `tmp/matcher-bytecode/`):

| pattern | engine | function |
|---|---|---|
| sequence of three, hit | 143 | 110 |
| sequence of three, miss | 61 | 33 |
| mapping, hit | 118 | 88 |
| eight-way literal, last | 167 | 93 |
| `let` shape over `(mylet [[x 1] [y 2]] body)` | 1103 | 788 |

Called from Python the function pays the context and evaluator entry,
about 250 ns, which the engine does not: 172 against 430 on the
sequence hit, 1146 against 1183 on the `let` shape. That is the cost
stage 2 meets with the demand host cache, and the one gate 3 watches.

Stage 2 as of 2026-10-14: `Matcher`, `MatchResult`, `match_select`,
`get_extern_defaults` and `reset_extern_defaults` are Python in
`koskari.matcher`; every importer reads them from there, and the C
registrations of the same names stay in `koskari.lib` until stage 5
removes the engine with them. A `Matcher` validates its tuple when
made (`validate_frozen`, the engine's rules without recursion) and
builds its function on the first match, through the `demand` wrapper
so the context is reused; its namespace is refreshed from the
defaults by their version. `match_select` checks the subject once and
calls each matcher's function bare, building a result for the winner
alone. From Python, per attempt, in ns: the engine 171, the bare
demand call 297, `Matcher.match` 623; a two-matcher select with the
second winning, the engine 264, Python 994; the `let` shape 1193
against 1366. One engine test went with the stage: a predicate that
wrote a car ahead of the cursor was unseen by the engine, which had
loaded the spine, and is seen by the live walk; the case was
pathological and carries no contract (ruled 2026-10-14). The sibling
that cuts the spine behind the cursor stays, as the memory-safety
case. Stage 2 complete 2026-10-14: 8332 tests across every phase.

Stage 3 as of 2026-10-14: the six syntax specials (`syntax-rules`,
`macro-rules`, `syntax-match`, `macro-match`, `special-rules`,
`special-match`) parse each rule's pattern at the defining compile and
lower it through the inline target into a lambda body, the subject its
one formal, every extern requested from the module under the atom the
pattern wrote, completed by `function_constant` as any lambda is. The
form lowers to a call of the `build-*` builtin with, per rule, that
function constant under `BUILD_FUNCTION`, a names tuple (the slot
names and the repeat variables as pairs), the quoted pattern source
and the template or handler lambda. The builtins keep their `Pattern`
entry beside the function entry, and a transform built from a function
holds the pattern's source for help text and errors. The named
captures instructions lower in `convert_fast` to the slot-run forms
when a scope's locals are one run of fast slots, which the lambda
body's are. A module-built matcher reaches its default externs
through the module's builtins, so a harness that compiles over a bare
namespace binds the defaults under the test's own definitions. The
serializer test that asserted the old lowering's artifact, the
literals frozenset the build call quoted, was dropped.

Stage 4 as of 2026-10-14: `Dispatcher` in `koskari.matcher` holds a
syntax's matchers and builds, on first use, one Function over them
through `compile_dispatch_function`: a frozen matcher lowers inline
with the next clause's entry as its miss edge, a matcher a module
built is called under a closure name of its own and its result
compared against None with `CHECK_COMPARE`, and a hit returns
`(values index captures)` through the `values` type stored in the
dispatcher's namespace beside the default externs. `Syntax` builds
its dispatcher lazily and anew for every composition, since `+` makes
a new `Syntax`; `match_select` and `Matcher.ready` are gone, and
`MatchResult` is built for the winning arm alone. The `let` special's
two arms dispatch in 1371 ns through the one function against 2129 ns
through two `Matcher.match` calls; the new `syntax-select` cell, a
three-arm syntax whose last arm is the let shape, reads 1648 ns beside
the one-matcher `syntax-dispatch` cell's 1381 ns.

Stage 5 as of 2026-10-14: the engine is gone from `koskari/lib`. The
builder, validator, thaw, the subject adapters, the node walk and its
change log, the defaults resolution, the root pre-test, the C
`Matcher` and `MatchResult` types and the `match_select`,
`get_extern_defaults` and `reset_extern_defaults` module functions
are removed, with the `KOS_CACHE_MATCHER` cache kind and the compat
helpers that read the defaults dict's version. `matcher.c` keeps
`Datum`, `DatumFrame`, the sequence cursor, the isinstance and PEP
634 gate helpers and the steps in `matcher/step.c`, which took the
mapping probe and the 3.9 `__match_args__` synthesis with it.
`MATCH_CASE` keeps its opcode and its handler raises. The native
matcher tests, `tests/lib/opcode/test_match_case.py` and the corpus
oracle test went with the engine they drove; the instruction tests
and the matcher suites over the Python `Matcher` cover the steps.

Stage 6 as of 2026-10-14: `OPCODES.md` section 9.2 and the VM
reference under `docs/reference/vm/` describe the `MATCH_*` family,
one entry per shape with its VAR and FAST twins, and mark `MATCH_CASE`
retired; `MATCHER.md` describes the three containers a compiled
pattern lives in, the `Syntax` dispatcher and the inline `match` arm;
`FLATTENING.md` lists match predicates and extractors among the calls
that flatten, and `resumable-reads.md` section 8 is marked superseded;
`SPECIALS.md`, `FEATURES.md`, `api/pattern-matching.md` and the user
pages for `compile/match`, the match-patterns guide and the glossary
no longer describe a frozen matcher constant, a native engine or a
synchronous predicate call.

Core types as of 2026-10-14, after the review: `TYPE` of a core
name lowers to a `("TEST", "CORE", ordinal)` node and the four
`MATCH_CORE_TYPE` shapes (`MATCH_CORE_TYPE`, `MATCH_CORE_TYPE_BIND`,
`MATCH_NEXT_CORE_TYPE`, `MATCH_NEXT_CORE_TYPE_BIND`, seven opcodes
with the twins), the ordinals `KosCoreType` in `matcher.h` exported as
`koskari.lib.CoreType`. The test harness no longer seeds the defaults
into a bare module namespace, since nothing in a syntax rule reads
them. The match_case churn kernel moved from 0.57x to 0.54x of Python.

Defaults removed as of 2026-10-14: `ExternDefaults`, the baseline
table, `get_extern_defaults`, `reset_extern_defaults`, `store_defaults`,
`Matcher.refresh` and the version compares in `Matcher.match` and
`Dispatcher.select` are gone. `Matcher.match` stores its keyword
externs into the matcher's namespace and refuses a call missing one;
`missing_externs` takes only the provided mapping; a `Dispatcher` is
refused at construction when a frozen clause declares an extern, since
a syntax passes none.

Review items as of 2026-10-14, the two blocking findings: a sequence
cursor over an exact list now reads the list's live size at every
step after the descent, in the end test, the rest, the reservation,
the collect, the iteration span and the slice, so a list that user
code shrinks mid-match is read as it is and never past its end; the
descent's recorded length serves the width precheck alone. The class
test and the field reads peel the Datum as every other test does, so
a class pattern over a pair spine matches its element, and a REST
with a suffix peels before it boxes. A predicate or extractor on a
pair spine receives the Datum: a runtime pair is a source spine, and
the reviewers' finding there records intended behaviour.

Review item 4 as of 2026-10-14: a repeat pass clears the slots of
every nested scope under its node after the iteration packs, so a
named group's slot one pass bound and the next did not is not carried
into the next pass's captures; `MATCH_ITERATION` keeps clearing its
own scope. The clears pack: `VAR_CLEAR2/3/4` and `FAST_CLEAR2/3/4`
join the run shapes, so a `clear_slots` of up to four slots, on a
failed branch, a missed optional or a repeat's fail path, is one
dispatch.

Review item 5 as of 2026-10-14: `SAME` resolves its name by frame
when the pattern lowers, through `MatchCompiler.resolve_slot`: the
root, a GROUP or a REPEAT iteration is a frame, a ONEOF branch shares
the frame around it, depth 0 is the innermost frame and depth 1 the
one enclosing it, as the engine's frame dicts read by name. A name
not bound at the asked depth is refused at lowering. The emitter is
unchanged.

Review item 7 as of 2026-10-14: the standalone function's formal is
spelled `match-subject`, which no keyword extern a caller can pass
spells, and `Matcher.match` takes its subject positional-only, so an
extern named `subject` is an extern like any other. Item 6, the
keyword externs stored into one namespace across a re-entrant call,
is left: no caller in the tree passes a keyword extern, and a Koskari
pattern's externs are module names.

Review item 11 as of 2026-10-15: a failed `(or ...)` alternative
clears a binding of the capture's name that existed before the match,
where a plain miss leaves the failed capture's value. Ruled intended,
open decision 3; a save and restore per alternative would cost a
temporary and two instructions per branch on every match, and dropping
the clear in the inline target would leak a failed branch's binding
into a repeat iteration's captures.

Review item 12 as of 2026-10-15: a predicate or extractor that
captures its continuation and re-enters the match through it is a
side-effecting predicate. Each resumption completes the match from
the position the capture held, with the cursor and mark entries of
its own segment, and sees the captures as the previous pass left
them, since the slots are the frame's locals; no further behaviour is
defined.

1. **The standalone target and the remaining shapes.** `MATCH_SAME`,
   `MATCH_NEXT_SAME`, `MATCH_NEXT_APPEND`, `MATCH_COLLECT`,
   `MATCH_CAPTURES`, `MATCH_ITERATION`, `MATCH_MARK`,
   `MATCH_RESET`; the `REPEAT`, `GROUP`, `OPTIONAL`, `SAME` and root
   lowerings; the emitter moved to `koskari/match_code.py` with its
   two targets. Gate: `tests/matcher` against the function.
2. **The Python path.** `Matcher` and `MatchResult` as Python classes
   in `koskari.matcher` over the function, the base namespace from the
   defaults, `match_select` in Python; `Transform`, the helpers, the
   layouts, the parser and the meta-matchers follow with no change of
   their own; the five names leave `koskari.lib` and importers
   retarget. Gate: the suite; `engine-*` and `syntax-dispatch` against
   the baseline with the demand host cache on the entry.
3. **The module path.** `define-syntax` and its kin lower the pattern
   to a lambda constant and pass it with the names to the `build-*`
   builtins, which also keep their `Pattern` entry. Gate: the hygiene
   and syntax suites, the `.kio` round trip of a module defining a
   macro, and import time of the bootstrap files.
4. **One function per `Syntax`**, returning `(values index captures)`;
   `match_select` goes.
5. **Removal.** The engine, adapters, builder, validator, thaw,
   defaults resolution in C, root pre-test, and the C `Matcher` and
   `MatchResult` types; `datum.c`, `step.c` and the gate and test
   helpers stay. `MATCH_CASE` keeps its opcode and handler, the handler
   raising once the engine behind it is gone; `test_match_case.py`
   goes with the engine it asserts. Container gates on every step.
6. **Documents.** `MATCHER.md`, `OPCODES.md`, `FLATTENING.md`,
   `resumable-reads.md` section 8, `api/pattern-matching.md`.

## 13. The walk's state on the operand stack

**Status:** implemented 2026-10-15.

### 13.1 Model

A cloned frame copies its operand segment and shares its namespace, so
state on the stack is per resumption and state in a local is per
frame. A cursor, mark or items list held in a local would be shared
by a continuation captured inside a predicate and resumed after the
frame moved on (review item 12), would outlive the form, and would
add to the function's local table per form.

The temporaries are entries on the operand stack: the rows push them
when they create them, write at a depth while they live, and pop them
when their extent ends. A row names a temporary by its distance under the
top of the stack, an immediate the lowering computes from the depth it
already traces. The segment is the storage, so nothing is allocated,
cloned or freed for them: a continuation copies the entries live at
the capture with the operands around them, a raise unwinds to the
enclosing try's recorded depth and drops them, and a miss edge pops
them on its way to the next arm as it pops the elements under test
today.

Captures stay locals. The body reads them after the walk is gone, a
repeat's iteration reads a group's slots, and a binding is a name,
not walk state.

### 13.2 Extents

- **Cursor.** `MATCH_FORM` pushes the cursor above the subject;
  `MATCH_NEXT_FORM` pushes the inner cursor; `MATCH_END` checks the
  cursor at its depth and, with its flag set, pops it, the top at that
  point since a pull leaves nothing. A nested form and an inline arm
  set the flag; the standalone root clears it and the captures read
  the cursor at depth 0 and the subject at depth 1, the frame taking
  both at its return.
- **Mark.** `MATCH_MARK` pushes the position; `MATCH_RESET` reads it
  at its depth; the node's hit path pops it, the top again, and the
  miss edge pops it with everything above the subject.
- **Items.** `MATCH_ITEMS` pushes the empty list; `MATCH_ITERATION`
  appends its value at the list's depth; after the loop the list is
  the top and goes into the capture slot or is popped.
- **Arms.** A miss edge pops the arm's cursor and marks on its way to
  the next arm, which pushes its own. Nothing is shared between arms
  and nothing is stale when one begins. A standalone function and a
  dispatcher push the subject once ahead of their clauses.

An entry's offset at an emission site is the count of entries pushed
above it there, elements under test and later temporaries alike,
which is the `depth` the lowering carries for its miss edges extended
to count the temporaries too. Every instruction inside a walk has a
static stack effect: the `MATCH_*` rows, the predicate call, and a
guard, net zero around its jump; a form nested in a guard pushes its
own entries above and reads only those.

### 13.3 Instructions

- The rows that name a cursor, mark or items entry take the depth as
  a `RAW` field: `MATCH_NEXT_FORM` (the outer cursor; the inner is
  pushed), `MATCH_NEXT`, `MATCH_PEEK`, `MATCH_NEXT_BIND`,
  `MATCH_NEXT_TYPE` and `_BIND`, `MATCH_NEXT_CORE_TYPE` and `_BIND`,
  `MATCH_NEXT_LITERAL`, `MATCH_REST` and `_BIND`, `MATCH_END` (and a
  pop flag), `MATCH_AVAILABLE`, `MATCH_NEXT_SAME`, `MATCH_NEXT_APPEND`
  (two), `MATCH_COLLECT` (two), `MATCH_MARK` (the cursor; the mark is
  pushed), `MATCH_RESET` (two), `MATCH_CAPTURES` (start and cursor;
  the first capture slot stays a fast slot) and `MATCH_ITERATION` (the
  cursor and a mode; its mark is the top and its items under that).
  `MATCH_FORM` names nothing: it gates the subject on the top and
  pushes the cursor. A capture or type field keeps its VAR and FAST
  twins. The peek rows are untouched.
- `MATCH_ITEMS` pushes a repeat's empty list; `MATCH_COUNT L, k, n`
  misses when the list at depth k is shorter than n.
- The handler reads an entry through `kos_stack_peek_under_fast` and
  writes it through `kos_stack_set_under_fast`, inlines whose underrun
  check sits behind the out-of-line path, as the other fast accessors.
  A read is a load at an offset from the top, with no slot array and
  no ref between. Nothing generic is added.

The rows carry these schemas in place: the names are those of the
family with `MATCH_ITEMS` and `MATCH_COUNT` added, and the VAR twins
of the rows whose only slot was a cursor are schema-identical to their
FAST twins.

### 13.4 Resumption

Each resumption of a continuation captured in a predicate has its own
entries, so a walk resumed twice walks on twice from the cursor it was
captured with. A pair cursor advances by replacement, the next cell
written at the entry. A sequence cursor advances in place when its
entry holds the only reference and by a new cursor otherwise; a
cloned segment and a mark each hold a reference, so neither sees an
advance made after it took the cursor. The refcount is the sharing
test: nothing is recorded, and the plain walk allocates nothing.

### 13.5 What it removes and costs

The namespace of a function with a match holds its captures and
nothing else of the walk: no cursor, mark or items local, no gensym
name for one in the `.kio`, no clear of one, no retention of one past
the form. The cost is the entries on the operand stack for their
extents, where a segment grows on demand and a spilled segment stays
with its cached frame, against fast slots allocated for every call of
the function, and the pops at the ends of extents against the clears
they replace. Measured 2026-10-15: the benchmark cells at parity or
ahead, the churn match_case kernel within its run-to-run spread, and a
dispatcher's clauses ahead once the subject is pushed once.

### 13.6 Set aside

Pooling the temporaries as locals was weighed and set aside: textual
non-overlap is not dynamic non-overlap once a continuation can leave a
form's extent and come back. A dynamic-chain node for the temporaries
was weighed and set aside: it would need a node kind with its clone,
traverse and free arms, an extent pair and a push and pop per form,
to hold what the segment already holds per resumption. A block
reserved at the form's entry and addressed by depth was weighed and
set aside: the rows that create a temporary can push it, so nothing
need exist before its first write and nothing is live past its
extent.
