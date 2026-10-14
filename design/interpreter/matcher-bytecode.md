# Matcher as bytecode

**Status:** proposal, 2026-10-13. A proof of concept is scoped in
section 11. The native engine remains the matcher until the
measurements in section 9 say otherwise.

**See also:** [../MATCHER.md](../MATCHER.md) (the engine, the pattern
vocabulary, the capture shapes), [../FLATTENING.md](../FLATTENING.md)
(the predicate and extractor entry this design closes),
[resumable-reads.md](resumable-reads.md) section 8 (the resume-row
engine this design supersedes), [../OPCODES.md](../OPCODES.md) section
1.8 (compression), [electors.md](electors.md),
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
inline into the function that contains a `match` form. Twenty-eight
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

- **standalone**: a `FunctionSpec` whose formals are the subject and
  one keyword formal per extern (section 2.7), returning the capture
  captures on a match and `None` on a miss; the `Matcher` surface,
  `match_select`, `match1`, and `define-matcher` are calls to it;
- **inline**: a fragment merged into the enclosing function by
  `compile/match`, captures stored into the clause's declared names,
  the clause bodies and guard lowering unchanged.

### 2.2 Pull and peek

A matcher instruction either **pulls** its element from a cursor slot
or **peeks** the element on top of the stack. A pull instruction
consumes from the cursor and leaves nothing on the stack. A peek
instruction never pops. A miss never pops. A failure edge therefore
lands with the stack the clause entered with, and `DUP` does not occur
in a lowered pattern: the runtime clause works on the subject in place
(the keep flag of `MATCH_CASE` with no flag), and a `ONEOF` over one
element pulls once, has each branch peek, and pops at the join or on
the miss edge.

### 2.3 Cursors

A cursor lives in a scratch fast slot, one per nesting depth, reused
across clauses. Over a pair spine the cursor is the cell itself; a pull
stores the cdr back and takes the car. Over a list, tuple or
`collections.abc.Sequence` the cursor is a small native object holding
the sequence and an index. `MATCH_FORM` decides which at descent from
the PEP 634 gate `match_form` applies today, and every pull instruction
reads either. No spine is pre-walked and no sequence is copied.

The pull instructions are the `ITER_NEXT_STORE_FAST` shape: cursor
slot read, advanced in place, element delivered, miss jumps with the
slot untouched. A sequence cursor advancing in place means a copy of
the slot's value is not a saved position; a step that must read ahead
without moving, the zero-width `NOT`, uses `MATCH_PEEK`, and a loop
that must retry from a position, the `REPEAT` of step 2, saves the
index rather than the cursor.

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
gives it a fast slot; `int` and `str` resolve the same way as a user
type. In the standalone shape each is a keyword formal. `CONST` fields
carry values only: the literal of a literal test, a mapping key written
as an atom, a field name. The engine's missing-binding `TypeError`
becomes the formals machinery's arity error; `missing_externs` is the
formals spec read back.

Boundary shift: there is no defaults table. `KosMatcher_ExternDefaults`,
`reset_extern_defaults`, the defaults version watcher, and the per-site
env cache have no successor. A pattern's `int` is the ref the closure
captured: a store into that ref reaches every site at once, a bind of a
new ref to the name reaches none of them, as for any closure.

### 2.8 Captures and repeat iterations

`MATCH_CAPTURES` builds the captures from a table of slot name and
fast index pairs, skipping an unbound slot, so the sparse captures dict
and an unfired `OPTIONAL` need no sentinel. On the syntax side it pops
the source cell for `.source` and `.position` and builds a
`DatumFrame`; on the runtime side it builds a dict. A `REPEAT`
iteration over a group ends in `MATCH_REPEAT_APPEND`, which builds the
group's captures from its scratch slots, appends them to the list in the
capture slot, and clears the scratch slots. A capture-less iteration
appends the boxed element (`MATCH_NEXT_APPEND`), and an `x ...` at the
end of a spine is one `MATCH_COLLECT`.

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
the miss label. `cur` names a cursor slot and `slot` a capture or
scratch slot; the compiler emits both as VAR fields and `convert_fast`
lowers the row to its FAST twin where the names have fast slots, as it
does for `ITER_NEXT_STORE_VAR` and the item family. At module scope
and past the fast-slot cap the VAR form runs and stores by name, as
`VAR_STORE` does. `type` is always a VAR field (section 2.7), lowered
with the rest of the row. `key` is a CONST for an atom key and a VAR
for a symbol key; `const` is a value. `widths` packs min and max into
one RAW field, eight bits each, `0xFF` unbounded, since a row immediate is
sixteen bits; a FORM's widths saturate safely, and a REST whose minimum
tail or suffix exceeds 255 is refused by the lowering.

| Instruction | Fields | Kind | Effect |
|---|---|---|---|
| MATCH_NEXT | L, cur | pull | push the element (section 2.4) |
| MATCH_PEEK | L, cur | pull | push the next element without advancing; what a zero-width NOT reads |
| MATCH_AVAILABLE | L, cur, needed | pull | at least `needed` elements remain: the reservation a suffix makes against a repeat, an optional or a branch |
| MATCH_NEXT_BIND | L, cur, slot | pull | VALUE, BIND, a bare rule variable |
| MATCH_NEXT_LITERAL | L, cur, const | pull | a keyword or literal element |
| MATCH_NEXT_TYPE | L, cur, type | pull | anonymous `(:: T)` |
| MATCH_NEXT_TYPE_BIND | L, cur, type, slot | pull | SYMBOL, CALLEDBY, PAIR, KEYWORD, `(x :: T)` in a spine |
| MATCH_NEXT_SAME | L, cur, slot | pull | a repeated rule variable |
| MATCH_MARK | cur, slot | none | the cursor's position saved: its cell, or its index |
| MATCH_RESET | cur, slot | none | the cursor moved back to a saved position |
| MATCH_NEXT_FORM | L, cur, inner cur, widths | pull | descend into the next element |
| MATCH_NEXT_APPEND | L, cur, slot | pull | box one element onto the list in slot |
| MATCH_COLLECT | L, cur, slot | pull | every remaining element boxed onto the list in slot; nil required |
| MATCH_REST_BIND | L, cur, min, slot | pull | BODY, `*: r`, a dotted tail |
| MATCH_REST | L, cur, min | pull | anonymous tail |
| MATCH_END | L, cur | pull | the cursor must be nil |
| MATCH_LITERAL | L, const | peek | literal test |
| MATCH_TYPE | L, type | peek | type test |
| MATCH_TYPE_BIND | L, type, slot | peek | the runtime root `(x :: T)`; a typed branch |
| MATCH_SAME | L, slot | peek | equality with a bound slot |
| MATCH_FORM | L, inner cur, widths | peek | the runtime root sequence pattern |
| MATCH_CLASS | L, type, positional count | peek | isinstance; one cache slot |
| MATCH_FIELD | L, name or position | peek | push the attribute for a sub-pattern |
| MATCH_FIELD_BIND | L, name or position, slot | peek | a field captured directly |
| MATCH_MAP | L | peek | the mapping gate |
| MATCH_KEY | L, key | peek | push the value for a sub-pattern; CONST and VAR key forms |
| MATCH_KEY_BIND | L, key, slot | peek | an entry captured directly; CONST and VAR key forms |
| MATCH_BIND | slot | peek | store the element, nothing popped |
| MATCH_CAPTURES | names, base, start, cur | push | the captures of a scope: the names tuple, the first of the scope's run of capture slots, the saved start and the cursor; a `DatumFrame` with the span as its source over a pair spine, a dict otherwise; a NAMED form reads the captures by name for the inline target |
| MATCH_ITERATION | names, base, start, cur | push | one repeat iteration's value: the captures when any slot fired, else the boxed span; clears the slots it read; the same NAMED form |

Thirty-one shapes. Every shape that names a slot or a type has a VAR
and a FAST entry in `opcodes.incl`, on the precedent of the `VAR_*` and
`FAST_*` families; the two key shapes have a CONST key and a VAR key
form besides; `MATCH_CAPTURES` and `MATCH_ITERATION` have the slot-run
form the standalone target emits and a NAMED pair the inline target
emits, whose CONST is the (keys, locals) tuple. Sixty-six opcodes as
built. The FAST entry is the one that runs in a function body.

Handler notes:

- **Miss semantics.** A pull miss leaves the cursor slot untouched and
  pushes nothing. A peek miss leaves the stack untouched. The lowering
  knows the stack depth at every miss site and emits the `POP` count
  on the edge where one is needed.
- **Pair cursor.** A pull on a non-nil pair stores the cdr into the
  slot and works on the car; on nil or a non-pair it misses. A
  `MATCH_END` on nil succeeds; on anything else it misses, which is
  the improper-spine miss of today.
- **Sequence cursor.** The cursor object holds the sequence, its
  length and the index. A pull past the length misses. `MATCH_REST`
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
- **MATCH_CLASS.** `match_class` as the body with the `__match_args__`
  resolution cached per type in the site's slot; the 3.9 synthesis
  stays behind its version gate. `MATCH_FIELD` and `MATCH_FIELD_BIND`
  follow with `match_one_field`'s miss rule; the positional index
  reads the cached tuple.
- **MATCH_MAP, MATCH_KEY family.** `matcher_mapping_gate` and
  `map_probe` as the bodies.
- **MATCH_CAPTURES, MATCH_REPEAT_APPEND.** The table is a CONST tuple of
  slot name and fast index pairs. `datum_frame_set_source` sets the
  source and position from the popped cell on the syntax side.
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
| FORM | MATCH_NEXT_FORM into the next cursor slot, the inner children, MATCH_END on the inner cursor |
| GROUP | MATCH_MARK, the children into the group's slots, MATCH_CAPTURES over the group's table; stored into the capturing slot or popped |
| ONEOF | branches all one element wide with no reservation: one MATCH_NEXT, each branch peeks, a branch's miss edge clears its slots and jumps to the next branch, the join pops; otherwise MATCH_MARK, each branch guarded by MATCH_AVAILABLE and pulled, its miss edge MATCH_RESET and clears, the last branch's miss edge the enclosing edge |
| OPTIONAL | MATCH_AVAILABLE against the reservation, MATCH_MARK, the inner node pulled; its miss edge MATCH_RESET and clears |
| REPEAT | `BUILD_LIST 0` into the capture slot or a temporary; a loop of MATCH_AVAILABLE against the reservation, MATCH_MARK, the inner node, MATCH_ITERATION, `VAR_LIST_APPEND`, `JUMP` back; the iteration's miss edge MATCH_RESET and clears; a `min_count` is `OBJECT_LEN` and `CHECK_COMPARE GE` after the loop |
| REST | MATCH_REST_BIND or MATCH_REST; with a non-empty suffix, the `remaining` count feeds the min |
| NOT | MATCH_PEEK, the inner node as peek instructions with its hit edge and miss edge exchanged, `POP 1`; captures are forbidden inside NOT already |
| CLASS, FIELD | MATCH_CLASS, then MATCH_FIELD_BIND per captured field or MATCH_FIELD, the sub-pattern, `POP 1` |
| MAP, MAP_ENTRY | MATCH_MAP, then MATCH_KEY_BIND or MATCH_KEY, the sub-pattern, `POP 1` |
| APP | MATCH_NEXT, `CALL 1` on the extern, the sub-pattern as peek instructions, `POP 1` |

## 5. Examples

`(define x 1)` against `MATCH(CALLEDBY(), SYMBOL("sym"), VALUE("expr"))`,
standalone. Formals put the subject in fast 0 and the extern `symbol`,
a keyword formal, in fast 1; fast 2 to 4 are the slots, fast 5 the
cursor.

```
 0  FAST                  0 (subject)
 1  FAST_STORE            5 (cur)
 2  MATCH_NEXT_TYPE_BIND  -> 7, cur, fast 1 (symbol), 2 (called-by)
 3  MATCH_NEXT_TYPE_BIND  -> 7, cur, fast 1 (symbol), 3 (sym)
 4  MATCH_NEXT_BIND       -> 7, cur, 4 (expr)
 5  MATCH_END             -> 7, cur
 6  FAST 0 ; MATCH_CAPTURES c#table ; RETURN
 7  CONST None ; RETURN
```

Today the same match walks the spine into two arrays, initialises the
engine, recurses three levels with a change record per capture, fills
a slot array, then builds the captures and a `MatchResult`.

`(match v [[#[a b *: r]] a] [[(x :: int)] when: (> x 3) x] [else: None])`,
inline. Today this is two `MATCH_CASE` instructions over frozen tuples
with a thaw and a slot map per site. `int` is a closure name in fast
6.

```
 0  FAST              0 (v)
 1  MATCH_FORM        -> 6, cur 5, (2 . unbounded)
 2  MATCH_NEXT_BIND   -> 6, cur 5, 1 (a)
 3  MATCH_NEXT_BIND   -> 6, cur 5, 2 (b)
 4  MATCH_REST_BIND   -> 6, cur 5, 0, 3 (r)
 5  POP 1 ; FAST 1 ; JUMP 11
 6  MATCH_TYPE_BIND   -> 9, fast 6 (int), 4 (x)
 7  CHECK_COMPARE_FAST  GT, fast 4, c#3, -> 9
 8  POP 1 ; FAST 4 ; JUMP 11
 9  POP 1 ; CONST None
11  RETURN
```

Instructions 7 to 11 are today's output unchanged.

`(my-or e ...)`, a `syntax-rules` pattern, which lowers to `CALLEDBY`
then `repeat_capture("e", TEST_ANY())`.

```
 0  FAST 0 ; FAST_STORE 4 (cur)
 2  MATCH_NEXT_TYPE_BIND  -> 6, cur, fast 1 (symbol), 2 (my-or)
 3  BUILD_LIST 0 ; FAST_STORE 3 (e)
 5  MATCH_COLLECT         -> 6, cur, 3 (e)
 6  FAST 0 ; MATCH_CAPTURES c#table ; RETURN
 7  CONST None ; RETURN
```

`[x 1] ...` under `let` is the loop form: a cursor save,
`MATCH_NEXT_FORM`, `MATCH_NEXT_TYPE_BIND` and `MATCH_NEXT_BIND` into
the group's scratch slots, `MATCH_END` on the inner cursor,
`MATCH_REPEAT_APPEND`, `JUMP` back; the iteration's miss label restores
the cursor from the save and falls out of the loop. The
`(expr as: name)` alternative of `BINDINGS` is where `MATCH_NEXT`, the
peek instructions and `MATCH_BIND` appear: one pulled element tried by
each branch and popped on the miss edge.

## 6. Consumers

- **`Syntax`.** All clauses lower into one standalone `Code` with a
  shared prologue; clause k's miss edge is clause k+1's entry; the
  function returns `(values index captures)`. One evaluator entry per
  form compiled instead of one engine run per clause tried. The Python
  `Syntax.compile_impl` unpacks the values object; a self-hosted one
  uses the values binding forms. Once clauses are instructions, the
  pure tests (literal, type, nil, length) can be hoisted into a shared
  ladder across clauses; predicate, extractor and field reads cannot,
  since they run user code in an observable order.
- **`compile/match`.** Emits the inline shape in place of the extern
  loads and `MATCH_CASE`; declares the capture names and the scratch
  temporaries; clause bodies, guards and `else:` unchanged.
- **Python entry.** `Matcher.match(subject, **externs)`, `match_select`,
  `match1` and the reader layouts call the standalone `Code` through
  `KosFunction_VectorCall`, which already adapts a Python call into
  the demand entry: `KosContext_New`, `koskari_eval`,
  `Py_EnterRecursiveCall` per call. Nothing matcher-specific crosses
  the boundary. The host-cache parameter of `demand_run_koskari`
  carries this entry if the baseline says it must. The entry
  disappears from the compile path when the compiler self-hosts.
- **Bootstrap order.** The lowering is Python with no `Compiler`
  dependency, so it is available at the point `koskari.matcher` is
  today: before the reader layouts and before the first `.ki` loads.
- **`.kio`.** A standalone matcher is a `FUNCTION_SPEC` constant; an
  inline one is instructions. The frozen-tuple constant kind in
  `serialize/constants.py` has no successor; a format bump.
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

1. Whether `koskari.lib.Matcher` survives as a type wrapping the
   `Code`, or `make_matcher` returns the callable and `MatchResult`
   goes; the eighteen Python files that touch `Matcher`,
   `from_tuple`, `match_select` or `MatchResult` follow that choice.
2. Decided 2026-10-13: every extern, `int` and `str` included, is a
   `VAR` field requested through `request_var`, so the closure
   apparatus brings the module binding into the function namespace at
   build. `CONST` carries values only. There is no defaults table.
3. Decided 2026-10-14: direct stores; a failed clause may leave a
   capture bound, as PEP 634 permits.
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
   MATCH_CLASS, MATCH_FIELD, MATCH_FIELD_BIND, MATCH_MAP, MATCH_KEY,
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
   MATCH_REPEAT_APPEND, source boxing, the cursor save and restore of a
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
- **The default externs** are the table `get_extern_defaults` returns,
  with `reset_extern_defaults`: `pair`, `symbol`, `keyword`, `int`,
  `str`, `tuple`, `list`, all of them builtins. They feed every matcher
  built outside a module.

### 12.2 Environments

A standalone matcher is a `Function` over a namespace, and its externs
are variables in that namespace, reached by the closure apparatus as
`match` reaches them. The compilation decides the namespace:

- a matcher built from Python gets a base namespace the compilation
  pre-populates from the default externs;
- a matcher built in a module closes over the module's namespace, so a
  type or predicate the pattern names is the module's binding, and the
  defaults are the builtins the module's chain already supplies.

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
3. **`match` and `case`.** Inline, as step 1 has it: the clause
   instructions interleave with the bodies and guards, captures are
   the enclosing function's locals, externs its closure names, cursors
   its temporaries. Nothing has a lifetime of its own.

Every matcher is instructions; the cases differ in container and
environment. Two of the three are the standard compiler lowering a
pattern it finds in source; the first is the one build that must stand
without it.

### 12.4 The captures epilogue

During a match the captures are fast slots and nothing else, one run
of them after the formals in the standalone target. `MATCH_CAPTURES`
packs them: its row is the names tuple (CONST, the slot table
`Matcher.slots` exposes), the fast index of the first capture slot
(FASTVAR, name `i` at `base + i`), the saved start position (FASTVAR,
from `MATCH_MARK`) and the cursor (FASTVAR). It skips an empty slot,
so an unfired `OPTIONAL` or the other branch of a `ONEOF` is absent;
a pair cell as the start makes the result a `DatumFrame` whose
`.source` is the span from the start cell to the cursor and whose
`.position` is the start cell's, an index as the start makes it a
plain dict. The root and a named `GROUP` are the same instruction over
different start and cursor slots; `MATCH_REPEAT_APPEND` is the same
read of a group's slots appended to the iteration list with the slots
cleared. The VAR twin reads each capture by name.

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
  is a fast slot or compile-time knowledge; the push and pop would
  cost about 10 ns on every attempt's hit path, and no teardown or
  continuation copy needs it.
- **A `let` around a clause's instructions** to keep the cursor
  temporaries out of the enclosing function's local table. Open; it
  costs a namespace and a region node per attempt, and is taken when a
  reason beyond tidiness appears.
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

1. **The standalone target and the remaining shapes.** `MATCH_SAME`,
   `MATCH_NEXT_SAME`, `MATCH_NEXT_APPEND`, `MATCH_COLLECT`,
   `MATCH_CAPTURES`, `MATCH_REPEAT_APPEND`, `MATCH_MARK`,
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
