# Matcher content backtracking

The matcher commits an OPTIONAL, REPEAT or ONEOF alternative once its
sub-pattern matches and never revisits it when the rest of the arm
misses. This note records the behaviour, its precedent, and the change
that makes those heads choice points. Found by the 2026-11 pre-public
review (D07); design discussed 2026-11-05.

---

## The behaviour

`koskari/matcher/analysis.py` picks an alternative by width alone: an
alternative that would consume an element the suffix needs at its
minimum is not taken, and otherwise the first alternative that fits is
taken. `koskari/matcher/emitter.py` then commits: OPTIONAL tests its
sub-pattern and, on a hit, pops its mark and falls into the suffix; a
later miss in the suffix is a miss of the whole arm.

```
match #(1 "s"):
  case #[(OPTIONAL a) (b :: int) *: r]: #(a b r)
  else: "no"
```

answers `"no"`, although `a` absent, `b = 1`, `r = ("s",)` satisfies
the pattern. The same holds for a typed OPTIONAL, for
`#[(REPEAT (k :: int)) (b :: int) *: r]` against `#(1 2 "s")`, and for
the spine subject `'(1 "s")`.

The user guide (docs/guide/match-patterns.md) does not state the commit
rule; the REPEAT docstring says only "Greedily match".

## Precedent

Width-only commit is the macro-system lineage. `syntax-rules` and
`syntax-case` give `(a ... b c)` the ellipsis length by arithmetic and
never retry; Shinn's `match` does the same count-based split; a PEG's
ordered choice is possessive, so `(a / e) b` never matches a lone `a`.
Those systems have no variable-width sub-pattern beside the ellipsis,
so the arithmetic is always exact. OPTIONAL and nested REPEAT make the
width a choice, and the current code takes the choice once.

Content backtracking is the regex lineage: regex itself, Racket's
`match` with several `...` in one list (its docs warn about the cost),
Racket's `syntax-parse` (`~optional`, `~or`, `~seq`), and Mathematica's
`BlankSequence`, the closest existing thing to "regex for s-exprs".

PEP 634 and Rust slice patterns sidestep the question with one star per
sequence.

## The equivalence

A leading OPTIONAL is an ordered disjunction with the suffix carried
into both arms:

```
#[(OPTIONAL a) (b :: int)]
#[(ONEOF (GROUP nil a (b :: int)) (GROUP nil (b :: int)))]
```

The rewrite is the PEG idiom for the same problem. It keeps captures
sparse (`a` unbound in the second arm), it keeps the first alternative
first, and it makes every arm fixed-width, so the width rule is exact
again. Its cost is the duplicated suffix: k optionals in a row give
2^k arms. REPEAT has no finite rewrite, since `(REPEAT p) rest` is
`ONEOF [p (REPEAT p) rest] [rest]` with itself on the right.

The change below is this rewrite with the suffix emitted once.

## Contract

OPTIONAL, REPEAT and a ONEOF whose branches consume elements are choice
points. Alternatives are tried in today's order: OPTIONAL present
first, REPEAT longest first, ONEOF left to right. A miss anywhere later
in the same arm resumes at the most recent choice point that has an
alternative left, with the cursor and the slots that alternative stored
put back. The arm misses when no choice point has an alternative left.

NOT stays committed. Negative lookahead is atomic in every engine; the
sub-pattern of a NOT is tested against a copy of the cursor and its
result is a verdict, not a position.

Consequences:

- Every pattern that matches today matches the same way with the same
  bindings. The first alternative is unchanged and a suffix that hits
  ends the search.
- Only today's misses can become hits. In a multi-arm `match`, an
  earlier arm can now win where a later arm fired before. This is the
  one visible change and the guide states it.
- The width rule becomes pruning, not decision: an alternative that
  cannot leave the suffix its minimum is dropped from the choice
  point's list before any test runs.
- Worst case is the same 2^k search the rewrite spells out, paid at
  run time only on a miss and only across the choice points actually
  taken.

## Mechanism

All in the emitter and analysis. No matcher node kinds change, and the
pattern surface is unchanged.

Marks are stack slots: MATCH_SCOPE pushes the cursor position and
MATCH_RESET puts the cursor back to one. Today OPTIONAL pops its mark
on the way out. A choice point keeps its mark on the stack until the
arm commits or the mark is spent.

- The suffix after a choice point is emitted once, at the stack depth
  that includes the live marks. Its miss target is the most recent
  choice point's retry label instead of the arm's miss.
- A retry label first tests whether its mark is live. Dead: jump to the
  next older retry label, or to the arm's miss when there is none.
  Live: MATCH_RESET to the mark, clear the slots the taken alternative
  stored (the existing `clear` on the alternative's scope), take the
  next alternative, kill the mark when that alternative is the last,
  and jump into the suffix.
- OPTIONAL has two alternatives: present, then absent. Its retry takes
  the absent arm and kills the mark.
- ONEOF on a cursor has one alternative per branch. Its retry resets
  and tries the next branch with the same suffix; the last branch kills
  the mark. ONEOF tested in place against one value never moves the
  cursor and needs no retry.
- REPEAT gives back one pass per retry. Each pass already appends its
  value to the items list; the position before each pass is recorded
  beside it, so a retry pops one item, resets the cursor to that
  position and re-enters the suffix with one pass fewer. Re-running the
  body with a smaller cap is not acceptable: it would re-execute user
  predicates and `::` tests. The minimum count bounds how far a REPEAT
  can give back; below it the mark is dead.
- A REPEAT whose body can match zero elements must advance the cursor
  on every pass or stop. The empty-iteration guard is what keeps every
  backtracking engine from re-creating the same choice point at the
  same position.
- The live-mark test is the one piece that may need an opcode or a
  MATCH_RESET variant that branches on a dead mark. Existing opcodes
  stay.

Termination: each choice point has a fixed finite list of alternatives
taken in order, and a resumed choice point never regains an alternative
it has spent. Each entry into the suffix spends one alternative, so
entries are bounded by the number of alternatives.

## Work

1. Analysis: keep `widths` and `suffix`; replace the alternative
   decision with the pruned alternative list per choice point.
2. Emitter: retry labels, marks kept live through the suffix, the
   REPEAT position record, the empty-iteration guard, and the stack
   depth bookkeeping for a suffix emitted under live marks.
3. Tests: the four shapes above (plain OPTIONAL, typed OPTIONAL, REPEAT,
   spine subject) red then green; a chain of optionals; a REPEAT at its
   minimum; a NOT that must stay committed; a multi-arm `match` where
   the earlier arm now wins. Corpus records for the same.
4. Docs: docs/guide/match-patterns.md states the retry rule and the
   arm-order consequence; the REPEAT and OPTIONAL docstrings say what
   happens on a later miss.
5. Perf baseline before the emitter edit: the emitted MATCH
   instructions run at eval time, and the specials' own argument
   patterns go through the same emitter.

Until this is done the commit rule is the behaviour and the guide
should say so.
