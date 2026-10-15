# Short-circuit jump threading

**Status:** proposal (2026-10-08), not implemented. One rewrite from a
wider control-flow cleanup that was set aside; it stands on its own,
and section 5 lists the opcodes that would extend its reach.

## 1. The shape today

`compile/and` emits each clause followed by `JUMP_AND done`, and
`compile/or` each clause followed by `JUMP_OR done`, with the last
clause bare and `LABEL done` after it. Both jumps pop the clause value,
test it, and on the short-circuit path push it back and jump; on the
fall-through path they drop it. The value at `done` is the form's
result on every path.

When the form is the test of a conditional, the row after `done` is a
conditional jump that pops that value and tests it again:

| emitter | row after the test |
|---|---|
| `compile/cond` (and `if`, `when`, `unless` through it) | `JUMP_FALSE` |
| `compile/while` | `JUMP_FALSE` |
| `compile/match` guard `when:` / `unless:` | `JUMP_FALSE` / `JUMP_TRUE` |
| a nested `compile/and` / `compile/or` | `JUMP_AND` / `JUMP_OR` |

So `(if (and (< a b) (< c d)) T E)` in a function with fast locals is:

```
    FAST            a
    FAST            b
    OBJECT_COMPARE  <, 2
    JUMP_AND        L1
    FAST            c
    FAST            d
    OBJECT_COMPARE  <, 2
L1: JUMP_FALSE      L2
    ...T...
    JUMP            L3
L2: ...E...
L3:
```

Eight dispatches on the all-true path, and neither compare fuses: the
squish rule `OBJECT_COMPARE JUMP_FALSE -> CHECK_COMPARE` needs the two
rows adjacent, and the first compare is followed by `JUMP_AND`, the
second by the label. The squish and fuse passes only look at adjacent
rows; `elide_join_constants` is the only pass that reads label
references, and it moves constants, not jumps.

## 2. The rewrite

A short-circuit jump whose label lands on a conditional jump is
replaced by the conditional jump the landing would perform on the value
the short-circuit op pushes. The value is known on the jumping path:
falsy after `JUMP_AND`, truthy after `JUMP_OR`. The landing row pops
exactly that value, so the rewritten row pops it instead and the stack
depth is unchanged on every path.

| row | landing | rewrite |
|---|---|---|
| `JUMP_AND L` | `JUMP_FALSE T` | `JUMP_FALSE T` |
| `JUMP_AND L` | `JUMP_TRUE T` | `JUMP_FALSE F` |
| `JUMP_AND L` | `JUMP_AND T` | `JUMP_AND T` |
| `JUMP_AND L` | `JUMP_OR T` | `JUMP_FALSE F` |
| `JUMP_OR L` | `JUMP_TRUE T` | `JUMP_TRUE T` |
| `JUMP_OR L` | `JUMP_FALSE T` | `JUMP_TRUE F` |
| `JUMP_OR L` | `JUMP_OR T` | `JUMP_OR T` |
| `JUMP_OR L` | `JUMP_AND T` | `JUMP_TRUE F` |

`F` is the label of the row after the landing jump: the landing's
fall-through. When no label is there one is inserted from the
compiler's label generator. The rewritten row is never a plain `JUMP`:
the short-circuit op pops its value on both of its paths, so the
replacement must pop too, and `JUMP_FALSE`/`JUMP_TRUE` to the
fall-through do that with a test whose outcome is fixed.

The landing is the first row after `LABEL L` that is not `POSITION` or
`NOOP`. A landing that is anything else leaves the row alone: a value
consumer (`(f (and a b))`), a `POP`, a `RETURN`, a `LABEL` of another
join, or an `OBJECT_NOT` (see section 5).

The pass repeats until a sweep changes no row. Each rewrite either
removes a `JUMP_AND`/`JUMP_OR` row or retargets one to a label further
along the chain, so the sweep count is bounded by the number of such
rows plus the chain lengths. A nested form threads once its inner
landing has been rewritten.

A label the pass leaves with no referrer is dropped. References are
read the way `elide_join_constants` reads them: a LABEL-style arg, or
LABEL fields of an EXTRA row. Only labels this pass orphaned go; every
other label stays, as today.

The pass runs before `squish`, so the pairs it creates fuse there
(`OBJECT_COMPARE JUMP_FALSE -> CHECK_COMPARE`, `NILP JUMP_FALSE ->
CHECK_NIL`, `EXCEPTION_MATCH JUMP_FALSE -> CHECK_EXCEPTION`,
`OBJECT_NOT JUMP_FALSE -> JUMP_TRUE`), and the fused rows take their
feeders in `fuse_feeders` and lower to the FAST twins in
`convert_fast` as they do now. The specials are unchanged.

## 3. Worked examples

The `and` test above, after threading, squish, and fusion. Two
dispatches on the all-true path:

```
    CHECK_COMPARE_FAST  <, a, b, L2
    CHECK_COMPARE_FAST  <, c, d, L2
    ...T...
    JUMP                L3
L2: ...E...
L3:
```

`(if (or (< a b) (< c d)) T E)`. The first clause's `JUMP_OR L1`
lands on `JUMP_FALSE L2`, so it becomes `JUMP_TRUE L4` with `L4`
inserted after that jump. Only the last clause fuses: nothing fuses a
compare with `JUMP_TRUE` (section 5.1). Four dispatches on the
first-true path, from eight:

```
    FAST                a
    FAST                b
    OBJECT_COMPARE      <, 2
    JUMP_TRUE           L4
    CHECK_COMPARE_FAST  <, c, d, L2
L4: ...T...
```

`(if (and (or a b) c) ...)` with bare variables. Before:

```
    FAST        a
    JUMP_OR     L1
    FAST        b
L1: JUMP_AND    L2
    FAST        c
L2: JUMP_FALSE  L3
```

Sweep one rewrites `JUMP_AND L2` to `JUMP_FALSE L3` and drops `L2`.
Sweep two finds `JUMP_OR L1` landing on that `JUMP_FALSE L3` and
rewrites it to `JUMP_TRUE L4`, inserting `L4`, and drops `L1`:

```
    FAST        a
    JUMP_TRUE   L4
    FAST        b
    JUMP_FALSE  L3
L4: FAST        c
    JUMP_FALSE  L3
```

Six dispatches either way here. The bare-variable shape gains only
with the feeder forms in section 5.2.

`(while (and (< i n) (ok? i)) ...)`. The loop test is `JUMP_FALSE
loop_exit`; the first clause threads to it and fuses to
`CHECK_COMPARE_FAST`, the call clause keeps its `CALL` and the
`JUMP_FALSE`.

## 4. Non-goals

The wider cleanup stays out: threading a
jump that lands on a plain `JUMP`, sweeping unreachable rows, sinking a
join's `POP`, inverting a branch that hops a `JUMP`. Only
`JUMP_AND`/`JUMP_OR` rows are rewritten, only labels they orphan are
dropped, and no row moves.

## 5. Opcodes that would extend the rewrite

The pass needs no new opcode. Three additions would let more of the
threaded shapes fuse, and two of them pay off on plain conditionals
with no `and`/`or` at all.

### 5.1 A jump-on-success polarity for the compare branches

`CHECK_COMPARE`, `CHECK_COMPARE_VAR` and `CHECK_COMPARE_FAST` branch on
failure only. An `or` clause threads to `JUMP_TRUE`, and a `match`
`unless:` guard emits `JUMP_TRUE` directly, so neither fuses. Inverting
the selector is not an answer: `not (a < b)` is not `a >= b` under
NaN or a user `__lt__`.

The CMPSRC field holds the selector in bits 0-3 and the two
constant-source bits in 4-5; bit 6 is free. A polarity bit there turns
the two exits of `CHECK_COMPARE_VAR`/`_FAST` around with one xor on the
compare result, no new opcode and no new handler. The bare
`CHECK_COMPARE` row has a NOARG fourth field that can carry the same
bit. The squish rule gains `OBJECT_COMPARE JUMP_TRUE -> CHECK_COMPARE`
with the bit set; `fuse_check_compare` passes the bit through. With it,
the `or` example above becomes two `CHECK_COMPARE_FAST` rows.

### 5.2 Truth-test branches with a feeder

`JUMP_FALSE` and `JUMP_TRUE` have no fused feeder form, so `(if x ...)`,
`(when x ...)`, `(while flag ...)`, and every bare-variable clause of a
threaded `and`/`or` spend a `FAST` dispatch to push a value the jump
pops at once. Four EXTRA opcodes, `JUMP_FALSE_VAR`, `JUMP_TRUE_VAR` and
their `_FAST` twins, with fields `(VAR|FASTVAR, LABEL, NOARG, NOARG)`,
take the feeder in `fuse_feeders` and lower in `convert_fast` through
the written-out map. The nested example in section 3 becomes three
rows:

```
    JUMP_TRUE_FAST   a, L4
    JUMP_FALSE_FAST  b, L3
L4: JUMP_FALSE_FAST  c, L3
```

This family applies to every conditional in the language, not only to
threaded sites, so it is the larger win of the three and stands as its
own piece of work.

### 5.3 Short-circuit jumps with a feeder

In value position, `(or x default)` and `(and x (f x))` keep their
`JUMP_OR`/`JUMP_AND`, each fed by a bare load. `JUMP_OR_FAST slot, L`
and `JUMP_AND_FAST slot, L` (EXTRA, `(FASTVAR, LABEL, NOARG, NOARG)`,
with VAR twins) would read the slot, test it, and push it only on the
jumping path. The handler does what `JUMP_OR` does today minus the
pop; the fall-through path touches no refcount.

### 5.4 The `not` landing

`(if (not (and a b)) ...)` compiles the form, then `OBJECT_NOT`, then
`JUMP_FALSE`. The landing is `OBJECT_NOT`, so the pass leaves it. The
squish rule later turns `OBJECT_NOT JUMP_FALSE` into `JUMP_TRUE`, but
squish runs after the pass. Resolving the landing through `OBJECT_NOT`
(a `JUMP_FALSE` behind a `NOT` is a `JUMP_TRUE` landing, and the
reverse) is a one-row extension of the landing search, kept out of
the first implementation.

## 6. Tests and validation

`tests/compile/test_thread_shortcut_jumps.py`, compile phase, driving
the pass alone through `ByteCodeProcessor` as the join-constant tests
do: the eight landings of section 2, the nested chain, the orphaned
label dropped and a still-referenced label kept, the fall-through label
inserted only when none is there, and a landing that is a value
consumer left alone. One end-to-end compile of the `and` example
asserting two `CHECK_COMPARE_FAST` rows and no `JUMP_AND` is the red
reproducer. The eval-level behaviour of `and`/`or` in test position is
already covered by the basics phase and the corpus.

Validation: the branch churn kernel under `perf stat`, baseline before
the edit; then `tox -qe py -- tests/compile tests/lib/opcode/test_jump.py`;
then `make lint test`.
