# Matcher core

Status: design; prep done, execution from phase 1.

## Why

The current lowering runs one pattern node through two strategies, peek
mode (the element is on the stack) and pull mode (the element is behind
a cursor), plus width shortcuts: the one-wide capture and ONEOF paths,
the length field of `MATCH_FORM`, `MATCH_AVAILABLE`, the trailing-ONEOF
yield rule. Each node's meaning is split across them. The review of
2026-10-16 (tmp/review-2026-10-16-r2/ROLLUP.md) traced most defects
there: a NOT after the element testing that element again, shapes the
validator accepts that the lowering cannot emit, refused `match` arm
roots, the depth trace refusing valid patterns, a yield rule that
depends on wrapping. Captures held in frame locals and cleared on miss
edges lose a resumed continuation's values and clear a user variable of
the same name; the capture locals are gensyms that reach the .kio.

The core replaces the instruction set and the lowering with one model:
every node runs against a cursor, every capture lives in a mark on the
operand stack, and every node leaves the stack as it found it on both
exits.

## Kept

- A matcher is a Koskari Function: a gensym positional formal, the
  captures on a match, None on a miss. The shared first-match function
  and a Syntax's arms are unchanged.
- Pattern and `MatchCompiler` produce the node tables (slots, externs,
  consts, nodes, entry). Pattern is a compile-time helper. Two changes:
  the slots a ONEOF's branches bind under one name share one index in
  their scope, and a branch's names stay visible to the nodes after the
  ONEOF (so a SAME after a ONEOF resolves).
- The walk's state is on the operand stack, addressed by depth.
- `KosMatchCursor`, its move to a new object when shared, its freelist.
- Datum boxing over pair spines; DatumFrame results.
- Externs are closure names over the matcher's namespace. The inline
  `compile/match` path; the `define-syntax` FunctionSpec path.
- `MATCH_CASE` (on master) is untouched.

## Replaced

- Every `MATCH_*` instruction this branch introduced. They are a
  proposal until the branch merges; the core set below takes their
  place. The ABI minor was already bumped on this branch; no further
  bump.
- The peek/pull split, the width shortcuts, the trailing-ONEOF yield
  rule, the depth-trace self-check, the miss trampolines keyed by pop
  count.
- Captures in frame locals, their gensym names and their `VAR_CLEAR`
  miss edges.

## The base node set

Element tests consume one element on a spine and read through a Datum
box:

| node | matches when |
|---|---|
| `TEST ANY` | always |
| `TEST LITERAL c` | the element is `c`, a gensym of `c`'s base, or equal to `c` |
| `TEST CORE n` | the element is the core type n (int, str, symbol, keyword, pair, tuple, list) |
| `TEST TYPE e` | `isinstance(element, e)` |
| `TEST PREDICATE e` | `e(element)` is true; on a pair spine `e` receives the Datum |
| `SAME slot depth` | the element equals the value of `slot` in an open scope |

Element projections test one element through a view of it:

| node | matches when |
|---|---|
| `FORM sub` | the element is a sequence (pair spine, tuple, list; not a str, mapping or iterator) and `sub` matches all of it |
| `APP e sub` | `sub` matches the single value `e(element)` |
| `CLASS e fields` | `isinstance(element, e)` and every field matches |
| `FIELD_POS sub` | `sub` matches the attribute at the next `__match_args__` position |
| `FIELD_KW sub name` | `sub` matches the attribute `name` |
| `MAP entries` | the element is a mapping and every entry matches |
| `MAP_ENTRY_CONST`, `MAP_ENTRY_EXTERN` | the key, a constant or an extern, is present and `sub` matches its value |

Spine composites act on the cursor:

| node | behaviour |
|---|---|
| `MATCH children` | the children in order on the same cursor |
| `GROUP g children` | the same, in a capture scope of its own; its captures become the value of `g` |
| `GROUP children` (anonymous) | the same, in a scope of its own whose captures are discarded |
| `ONEOF branches` | the first branch that matches; a failed branch restores the cursor and empties its slots |
| `OPTIONAL sub` | `sub` if it matches, else zero elements |
| `REPEAT sub min` | `sub` repeated, at least `min` times; each pass is one iteration |
| `REST min_tail` | the remaining elements less the fixed suffix after it, as a span |
| `NOT sub` | zero width: succeeds when `sub` does not match at the cursor; the cursor never moves |
| `CAPTURE slot sub` | `sub` matches; `slot` binds what `sub` produced |

A whole matcher is a root that reaches the end of its subject. A single
value -- a `match` arm's subject, an APP result, a FIELD or MAP value --
is a one-element subject walked through a cursor like any spine.

## Semantics

- **Reservation, not backtracking.** A REPEAT runs on a bounded cursor
  that ends `w` elements before its parent's end, `w` being the minimum
  width of everything after the REPEAT on the same cursor, out to the
  enclosing FORM or root. The REPEAT takes passes until its body misses
  or the bound is reached; the suffix matches what is left. No element
  the suffix needs at its minimum is pulled by the REPEAT's body. REST
  takes the remaining elements less the fixed width of what follows it.
  A REPEAT followed by a REST: the REPEAT collects the elements its body
  matches, and the REST takes the tail after the first miss.
- **Length precheck.** A FORM, a root, and every same-cursor composite
  (MATCH, GROUP, ONEOF branch) whose minimum width exceeds 1
  checks the remaining length against its widths plus `w` before it
  tests an element. This is master's per-element reservation: a subject
  too short for a branch never reaches the branch's PREDICATE or APP.
- **The end rule.** NOT and OPTIONAL test their sub-pattern at the end
  as anywhere else; a pull at the end is an ordinary miss of the
  sub-pattern. `NOT(OPTIONAL x)` at the end misses.
- **Captures.**
  - CAPTURE over a REPEAT binds the REPEAT's items list; over a REST, the
    span; over a sub-pattern of static width one, the element; over any
    other, the span it consumed.
  - A slot inside an uncaptured REPEAT binds the list of its per-pass
    values (the `x ...` convention).
  - A failed ONEOF or OPTIONAL branch empties the slots it could have
    bound.
  - SAME reads a slot of an open scope: `(slot, depth)` names the
    `depth`-th enclosing scope. The branches of a ONEOF share the
    enclosing scope, so a SAME after the ONEOF reads a slot an earlier
    branch bound, and misses when the slot is empty.
  - Nothing outside the stack is written during a walk. A resumed
    continuation sees its own captures.
- **Inline arms.** On a hit, the root scope's filled slots are written
  to the arm's variables; an empty slot leaves its variable as it was.
  A missed arm writes no variable.

## Refusals at build

`check_nodes` keeps its refusals and adds these; each raises at build
(`Pattern.build`) or as a SpecialError at the pattern's position
(`compile/match`, `define-syntax`):

- a sub-pattern in a single-value position (a `match` arm root, an APP
  or MAP value) whose width range does not include 1 -- a bare NOT, or
  a sub-pattern of minimum width above 1; the case dialect writes
  `(not p)` as `MATCH(NOT p, ANY)`;
- a FIELD sub-pattern whose width is not exactly 1 (master's rule);
- a LITERAL whose value is not a pool constant, on every path (build,
  `compile/match`, `define-syntax`);
- a REPEAT whose body has minimum width 0;
- a node of variable width after a REST on the same cursor (a REST's
  length is the remaining elements less a fixed suffix);
- a CAPTURE anywhere in a NOT subtree;
- a SAME naming a slot outside every open scope (a closed sibling
  GROUP's slot is read through the group's value).

## Captures: the mark is the accumulator

- A capture scope -- the root, a GROUP node, one REPEAT pass -- pushes
  a mark. In the node tables a named group is a CAPTURE over a GROUP:
  the CAPTURE stores the GROUP's captures. A GROUP that is neither
  captured nor a REPEAT body opens a scope whose captures are discarded
  when it closes, as on master; it counts as a scope for SAME depth.
  ONEOF, OPTIONAL, NOT and CAPTURE push a mark with no slots.
- A mark records the cursor's position and holds an array of slot
  entries for its scope; an empty entry is unbound. The scope's slot
  count is known at lowering; the instruction that pushes the mark
  carries it and the mark is allocated at that size. Default capacity
  16; a larger scope gets a mark of its exact size. A mark never resizes
  during a walk. The freelist keeps default-size marks.
- A capture stores at a fixed depth (its scope's mark) and a fixed index
  (the slot's position in the scope). A stored Datum gets the slot's name
  as its binding, as on master.
- Every write to a mark (store, clear) copies the mark and its array
  first when the mark is held beyond its stack entry, and writes the
  copy back to that entry; items lists follow the same rule on append.
  A resumed continuation writes into its own copy.
- Leaving a named GROUP builds its captures from the filled entries,
  keyed by the scope's names (a constant on the instruction): a dict,
  or over a pair spine a DatumFrame whose source is the span from the
  mark's position to the cursor and whose position is the span's first
  cell. The DatumFrame references the span, never the mark. The value
  is stored into the parent scope's slot for the group.
- A REPEAT pass with slots builds a dict from its filled entries (over
  any subject, as on master) and appends it to the REPEAT's items list.
- The root's captures are the standalone matcher's result, or are
  written to an inline arm's variables.
- Popping a mark discards its scope's captures.

The stack holds the marks and the marks hold the captures: the frame
namespace is not written during the walk, a store is one indexed write,
and a miss discards by popping.

## Instructions

Names are provisional. `cur`, `m`, `s` and `items` are depths below the
top of the stack; `miss` and `label` are jump targets.

| instruction | stack | behaviour |
|---|---|---|
| `MATCH_DESCEND miss` | `v` -> `cur` | sequence gate (reads through a Datum); cursor over `v`; a pair spine is counted once and the count kept, a list or tuple is read at its live length; misses with `v` consumed |
| `MATCH_SINGLE` | `v` -> `cur` | one-element cursor over `v` |
| `MATCH_FITS cur min max miss` | -- | misses unless `min <= remaining <= max`; `max` 0xFFFF is unbounded; `FITS cur 0 0` is the end test |
| `MATCH_NEXT cur miss` | -> `e` | pulls one element, a Datum over a pair spine; misses at the end |
| `MATCH_BOUND cur w miss` | -> `cur'` | a cursor at `cur`'s position ending `w` before `cur`'s end; misses when fewer than `w` remain |
| `MATCH_SYNC cur c` | -- | moves `cur` to `c`'s position |
| `MATCH_TAIL cur w min miss` | -> `span` | the remaining elements less `w`, advancing `cur`; misses below `min` |
| `MATCH_SCOPE cur k` | -> `m` | a mark at `cur`'s position with `k` empty slots |
| `MATCH_RESET cur m` | -- | moves `cur` back to `m`'s position |
| `MATCH_AT m` | -> `e` | the element at `m`'s position, as `NEXT` gave it |
| `MATCH_SPAN m cur` | -> `span` | the span from `m`'s position to `cur` |
| `MATCH_STORE s i` | `v` -> | stores into slot `i` of the mark at `s` |
| `MATCH_CLEAR s slots` | -- | empties the slots (a CONST tuple of indices) |
| `MATCH_CAPTURES s cur names` | -> `caps` | a dict, or a DatumFrame over a pair spine |
| `MATCH_LOAD s i skip` | -> `v` | slot `i` of the mark at `s`; jumps to `skip`, pushing nothing, when the slot is empty |
| `MATCH_ITEMS` | -> `items` | an empty list |
| `MATCH_APPEND items` | `v` -> | appends |
| `MATCH_MIN items n miss` | -- | misses below `n` items |
| `MATCH_COLUMNS items s slots` | -- | stores, for each slot, the list of its per-pass values into the mark at `s` |
| `MATCH_LITERAL c miss` | `e` -> | `c` a pool CONST |
| `MATCH_CORE n miss` | `e` -> | `n` a RAW ordinal |
| `MATCH_TYPE_VAR t miss` / `MATCH_TYPE_FAST` | `e` -> | `t` a closure name; `convert_fast` and the standalone target rewrite `_VAR` to `_FAST` |
| `MATCH_SAME s i miss` | `e` -> | equal to slot `i` of the mark at `s`; misses when empty |
| `MATCH_FIELD_POS_VAR t i miss` / `_FAST` | `o` -> `o a` | the attribute named by `t.__match_args__[i]`, `t` the CLASS's closure name; TypeError when `i` is past the end, as on master |
| `MATCH_FIELD_KW name miss` | `o` -> `o a` | the attribute `name` |
| `MATCH_MAPPING miss` | `o` -> `o` | mapping gate |
| `MATCH_KEY k miss` | `o` -> `o v` | `k` a pool CONST |
| `MATCH_KEY_VAR k miss` / `MATCH_KEY_FAST` | `o` -> `o v` | `k` a closure name |

PREDICATE and APP use `CALL_VAR`, `JUMP_FALSE`, `DUP`, `POP`.
`MATCH_DESCEND`, `MATCH_CORE` and `MATCH_SAME` are primitives. A fused instruction combines a cursor operation with a
test (`NEXT` with `LITERAL`); fused instructions wait for the core to
pass and a benchmark to justify each.

## Lowering

Conventions: a node leaves the stack as it found it on a hit and on a
miss; a node that pushes has a fail label that pops what it pushed and
jumps to its parent's miss. `w` is the suffix width: its minimum for a
REPEAT, its fixed width for a REST; `s` is the
enclosing scope's mark; `c'` is a cursor the node pushed.

```
TEST ANY          NEXT cur miss; POP
TEST LITERAL c    NEXT cur miss; LITERAL c miss
TEST CORE n       NEXT cur miss; CORE n miss
TEST TYPE t       NEXT cur miss; TYPE_VAR t miss
PREDICATE e       NEXT cur miss; CALL_VAR e 1; JUMP_FALSE miss
SAME slot d       NEXT cur miss; SAME s_d i miss
FORM sub          NEXT cur miss; DESCEND miss; FITS c' min max f
                  <sub on c'> (-> f); FITS c' 0 0 f; POP
                  f: POP; JUMP miss
APP e sub         NEXT cur miss; CALL_VAR e 1; SINGLE
                  <sub on c'> (-> f); FITS c' 0 0 f; POP
                  f: POP; JUMP miss
CLASS e fields    NEXT cur miss; DUP; TYPE_VAR e f1
                  per field: FIELD_* f1; SINGLE; <sub on c'> (-> f2); FITS c' 0 0 f2; POP
                  POP
                  f2: POP; f1: POP; JUMP miss
MAP entries       NEXT cur miss; MAPPING f1
                  per entry: KEY k f1 | KEY_VAR k f1; SINGLE; <sub on c'> (-> f2); FITS c' 0 0 f2; POP
                  POP
                  f2: POP; f1: POP; JUMP miss
MATCH children    [FITS cur min+w max+w miss]; <children in order on cur>
GROUP (anonymous) SCOPE cur k; [FITS]; <children> (-> f); POP  ; captures discarded
GROUP g children  SCOPE cur k; [FITS]; <children> (-> f); CAPTURES s' cur names; STORE s i_g; POP
                  f: POP; JUMP miss
ONEOF b1..bn      SCOPE cur 0
                  [FITS b1]; <b1> (-> n1); POP; JUMP done
                  n1: RESET cur m; CLEAR s slots(b1); [FITS b2]; <b2> (-> n2) ...
                  nn: RESET cur m; CLEAR s slots(bn); POP; JUMP miss
OPTIONAL sub      SCOPE cur 0; <sub> (-> back); POP; JUMP done
                  back: RESET cur m; CLEAR s slots(sub); POP
NOT sub           SCOPE cur 0; <sub> (-> ok); RESET cur m; POP; JUMP miss
                  ok: RESET cur m; POP
REPEAT sub min    ITEMS; BOUND cur w f
                  head: FITS c' 1 max exit
                        SCOPE c' k; <sub on c'> (-> pass_miss)
                        CAPTURES s' c' names | AT m | SPAN m c'
                        APPEND items; POP; JUMP head
                  pass_miss: RESET c' m; POP
                  exit: SYNC cur c'; POP; MIN items min f
                  captured: the items list stays for the CAPTURE
                  uncaptured, k > 0: COLUMNS items s slots; POP
                  uncaptured, k = 0: POP
                  f: POP; JUMP miss
REST              TAIL cur w min miss; the span stays for the CAPTURE, else POP
CAPTURE slot sub  sub is REPEAT or REST: <sub>; STORE s i
                  otherwise: SCOPE cur 0; <sub> (-> f); AT m | SPAN m cur; STORE s i; POP
                  f: POP; JUMP miss
```

Roots:

```
standalone        VAR subject; DESCEND m0; FITS c min max f1; SCOPE c k
                  <root on c> (-> f2); FITS c 0 0 f2; CAPTURES s c names; RETURN
                  f2: POP; f1: POP; m0: CONST None; RETURN
inline arm        DUP; SINGLE; SCOPE c k
                  <root on c> (-> f2); FITS c 0 0 f2
                  per root slot i: LOAD s i skip_i; <store to its variable>; skip_i:
                  POP 2
                  <arm body>
                  f2: POP; POP; JUMP next_arm
```

The subject stays on the stack across arms (`DUP`). Each root slot is
written by an ordinary store the compiler emits for that name -- the
store an assignment in the arm would emit -- so it follows that
variable's path (a fast local, a cell, a module variable) and
`convert_fast` treats it as any other store. Only root slots become
variables; names inside a group or REPEAT are reached through the
group's or REPEAT's value. A named
group's variable receives its dict or DatumFrame; a REPEAT's variable
receives its items list.

NOT is a mark, a try, a reset and an inversion; at the end of a spine
`NEXT` misses inside the sub-pattern, so the end rule needs nothing of
its own. `(MATCH x (NOT 1))` on the single value `1` pulls `x`, the
NOT's `NEXT` misses at the end, and the arm hits.

## Acceptance

Master still carries the native engine. A differential harness runs
generated patterns and subjects through master's engine, in its own
process from master's build environment, and through the core, compares
serialized outcomes and captures, and requires them identical except
for these deliberate changes:

- an improper subject is a miss;
- a pattern nested past the depth limit, and a non-pool LITERAL, are
  refused at build;
- the refusals listed above;
- an element is read when it is pulled (a car edited ahead of the
  cursor is read as edited);
- a missed inline arm writes no variable, and a hit leaves the
  variable of an empty slot as it was;
- a standalone matcher's root subject that is not a sequence misses
  (master raised TypeError, or iterated a mapping's keys);
- a slot inside an uncaptured REPEAT binds the list of its per-pass
  values (master kept the last pass); a SAME after the REPEAT compares
  against that list;
- a pair LITERAL in a `define-syntax` or `compile/match` pattern is
  refused (it already failed to reach a .kio).

Newly accepted shapes get hand-written expected results. The harness is
a permanent test. matchbench and churn baselines are recorded on the
current branch before the core replaces it and measured after; they
are recorded, not a gate.

## Decisions from the prep inventory

- A named group is a CAPTURE over a GROUP node in the tables. A GROUP
  that is neither captured nor a REPEAT body opens a scope whose
  captures are discarded (decided in phase 2, as on master).
- A SAME at depth 0 inside a REPEAT pass that names a slot of an outer
  scope is refused (master's "outside its frame").
- An element is read when it is pulled. A list or tuple is read at its
  live length; a list that shrinks under the walk raises "match subject
  changed under the walk" as today. A pair spine is counted once by
  DESCEND; a loop found by the count raises as today. A car edited
  ahead of the cursor is read as edited (master read it when the
  subject was built; added to the acceptance exceptions).
- A stored Datum's binding is the slot name, as on master.
- Width and count fields are 16-bit RAW; 0xFFFF is unbounded in FITS;
  a fixed width or count above 0xFFFE is refused at build.
- `koskari/syntax.py`'s `_repeat#<hex>` slot names are replaced by
  plain names derived from the pattern position, so no gensym spelling
  reaches a .kio through a names constant.
- `VAR_CLEAR2-4` / `FAST_CLEAR2-4` were added on this branch for the
  matcher's clear runs. Phase 6 probes whether any other code produces
  their runs (consecutive `compile/delq`); if none does, they are
  removed with their pages and tests.
- The RAW field kind's definition moves out of VM index section 9.2 to
  the field-kind section in phase 8; `CHECK_COMPARE` uses it.
- `MATCH_CASE` keeps the branch's form (no cache slot, a handler that
  raises).

- Decided in phase 2:
  - a name binds at most once in a scope, except across the
    alternatives of one ONEOF; a second binding (a branch name reused
    after its ONEOF, a REPEAT column equal to an outer name) is refused
    at build, so a failed branch's CLEAR never empties another binding;
  - phase 6's `MatchCompiler` changes also make `resolve_slot` accept a
    SAME before its binding.
- Decided in phase 1, from the oracle against the current path:
  - a REPEAT pass's captures are a dict, as on master, over any subject;
  - a SAME that reads a slot of an open scope before the slot is bound
    is accepted and misses while the slot is empty;
  - a CAPTURE over a sub-pattern that may consume nothing is accepted
    and binds the (possibly empty) span, as on master; the branch's
    refusal from 9f8daa34 goes;
  - NOT testing the element after its position, every arm root kind,
    and no yield rule match master already, so they are not acceptance
    exceptions.

## Implementation plan

The core is built beside the current matcher and replaces it in one
switch (phase 6). About 183 matchers are built when the package
imports (every `@special`, the parser matchers), so the current matcher
stays live until the switch and `import koskari` works throughout.
Every behaviour that worked before works under the core at the end,
except the acceptance exceptions.

Prep artifacts in tmp/matcher-core/: INVENTORY.md (code map with a
verdict per name, the table format per node tag, tests classified KEEP
/ KEEP* / REWRITE / REPLACE / REMOVE, docs map, baselines),
build-master.sh and run-master.sh (master's engine in its own
environment), fuzz/ (the review generators and recorded outputs).

### Phase 0 -- baselines (done)

At 8f1aec3e: `make test` 8573 passed, 13 skipped; matchbench
match-first-arm ~45, engine-seq-hit ~147, syntax-dispatch ~880,
syntax-select ~1700 ns/op (four-cell runs); churn match_case 0.58x.
Logs in tmp/matcher-core/.

### Phase 1 -- the oracle corpus and test

- Extend the review generator (fuzz/review-r2/fuzz_gen.py) to the base
  node set and subject kinds, including inline `match` arms.
- Run it once under master (run-master.sh) and store patterns, subjects
  and serialized outcomes as tests/matcher/data/oracle corpus files.
- A comparison module (tests/matcher/oracle.py) builds each pattern on
  the branch and compares, applying the acceptance exceptions as named
  rules. The test module that runs it, tests/matcher/test_oracle.py, is
  added in phase 5 against the core path; until then the comparison is
  run by hand.
- Gate: `make lint test` green; the comparison runs against the current
  path and its divergences are recorded in tmp/matcher-core/STATUS.md.

### Phase 2 -- analysis (koskari/match_core.py, new)

- One pass over the tables produces per node: static widths and
  fixedness; the suffix width `w` (minimum after a REPEAT, fixed after a
  REST) out to the cursor's owner; the scope and slot index (merging a
  ONEOF's per-branch indices of one name; anonymous GROUPs merged into
  the enclosing scope); each ONEOF and OPTIONAL branch's slot set; SAME
  resolution; an uncaptured REPEAT's column slots; FITS placement.
- The refusals at build, read from the analysis, with `check_nodes`'s
  current refusals folded in and the overlaps resolved as stated in
  "Refusals at build".
- Gate: `make lint test` green (the module is not yet reached).

### Phase 3 -- native runtime (koskari/lib/matcher/, matcher.h), additive

- Cursor over a pair spine (current cell, remaining count), an end bound
  for BOUND, SYNC; sequences read live.
- `KosMatchMark`: a new variably sized type in a new fragment
  koskari/lib/matcher/mark.c (listed by setup.py's `matcher/*.c`
  depends glob): position, slot count, slot array; default capacity 16;
  a freelist of default-size marks; copy before a write when shared,
  the copy written back to the stack entry; captures built from filled
  slots and a names tuple (dict or DatumFrame); Datum binding set on
  store.
- Native tests for each helper in tests/native/test_matcher.c.
- Gate: `make test`; `make test-native test-asan test-valgrind` after
  touching koskari/lib/matcher.c (tests.mk does not track fragments).

### Phase 4 -- instructions, additive

- New rows in koskari/lib/opcodes.incl under a temporary `MATCH2_`
  prefix, so they do not collide with the current rows; renamed to
  `MATCH_` in phase 6. Field kinds: CONST for pool constants and names
  tuples, VAR for closure names (with `_FAST` twins), RAW for depths,
  widths, counts and ordinals, LABEL for targets.
- Handlers in koskari/lib/opcode/, bodies in koskari/lib/eval/
  eval_match.c over the phase 3 helpers; `tox -qe handlers`.
- tests/lib/opcode/test_match_core_instructions.py: each instruction's
  stack effect, hit and miss, copy on share.
- Gate: as phase 3.

### Phase 5 -- the emitter (koskari/match_core.py)

- One recursive `emit(node, cur, miss)` following the lowering table and
  the stack-neutral convention, over the existing `FunctionTarget` and
  `FragmentTarget`; the standalone root and the inline arm root.
- A temporary selector chooses the core path for
  `compile_match_function`, `lower_clause` and `lower_matcher`; tests
  and the oracle run with it on; it is removed in phase 6.
- tests/matcher/test_oracle.py added, running the corpus on the core
  path.
- Gate: `make lint test` green with the selector off; the oracle test
  and the KEEP tests green with it on (`tox -qe py` with the selector
  set), apart from the listed flips.

### Phase 6 -- the switch

- Every path runs on the core: `Pattern.build`, `compile/match`, the
  `define-syntax` specials, the Syntax first-match function. The
  selector goes.
- `MatchCompiler` gets its two changes (one slot index per name across
  a ONEOF's branches; branch names visible after the ONEOF).
- Removed: the current lowering in koskari/match_code.py (peek and pull
  paths, trampolines, depth trace, capture locals), every current
  `MATCH_*` row and its handler, body, helpers and tests (INVENTORY.md
  REPLACE / REMOVE), the `validate_capture_run` names in bytecode.c;
  `MATCH2_` renamed `MATCH_`; the VAR_CLEAR2-4 probe acted on.
- koskari/dis.ki renders the new operands; its two underscore-prefixed
  names are renamed.
- Gate: `make lint test`; native gates; the oracle test; a `.kio` round
  trip of a `define-syntax` module and of a runtime `match`.

### Phase 7 -- tests

- REWRITE (INVENTORY.md B): internal-table, instruction-listing,
  gensym-name and depth-trace tests become match/miss tests; the three
  depth-trace cases that are the only coverage of a behaviour are kept
  as behaviour tests.
- KEEP* flips: test_match.py's two missed-arm tests now expect the
  variable untouched.
- New: reproducers for D1, D3-D7, D10, R2-R5; each refusal; resumption
  twice inside a REPEAT, a ONEOF and an OPTIONAL; an inline arm leaving
  variables untouched on a miss and on an empty slot; a predicate
  looping the spine ahead of the cursor (terminates by the count).
- Gate: `make lint test`, native gates, oracle.

### Phase 8 -- docs

- docs/reference/vm/opcodes/: pages for the core instructions, removed
  instructions' pages deleted; VM index section 9.2, the quick
  reference rows and the family list rewritten; the RAW kind defined in
  the field-kind section.
- docs/reference/specials/compile-match.md: a missed arm writes no
  variable; every root kind; the `*: rest` example corrected.
- docs/guide/match-patterns.md; design/OPCODES.md rows.
- Gate: `make docs`; the index-vs-pages check.

### End point

Phase 8 is the end of this work. After it, the driver records
matchbench (four-cell runs) and churn against phase 0 and stops. Any
fused instruction, and the resumption of the halted review rollup for
the items the core does not subsume, are proposed to the maintainer,
not implemented.

## Execution

- Each phase is implemented by an Opus agent, one per phase, given this
  document, INVENTORY.md and STATUS.md. The driver reviews each diff and
  runs the gate before committing.
- A phase commits when its gate passes; a phase may commit in steps.
  Commit messages follow .claude/rules/git.md. No push.
- Progress, the last commit per phase and open issues are kept in
  tmp/matcher-core/STATUS.md. This document changes only to record a
  decision.
- Stop and report to the maintainer, and wait, when:
  - a behaviour that worked before cannot be kept and is not on the
    acceptance exceptions;
  - an existing test expectation breaks that is not in INVENTORY.md's
    REWRITE / REPLACE / REMOVE sets or the listed flips;
  - the oracle shows a divergence from master that no exception covers;
  - the design is silent and two readings lead to materially different
    work;
  - an opcode that exists on master needs to change;
  - a git operation beyond add, commit, mv and rm is needed.
- Binding rulings: CONST holds builtin pool values only, and a value the
  pool does not admit is refused; a type is a closure name, never a
  CONST; Koskari-to-Koskari calls flatten; no opcode suspension; no new
  underscore-prefixed names; no one-line helpers; user docs state the
  contract; "instruction" for bytecode ops, "row" only for the KosExtra
  operand record.
