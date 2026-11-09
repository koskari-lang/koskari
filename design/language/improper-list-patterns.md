# Patterns over improper lists (investigate)

A dotted `syntax-rules` pattern `(m a . rest)` binds `rest` to the
remaining elements as a proper list. It does not match an improper
subject: `(m 4 . 5)` misses, where a Scheme author expects `rest` to be
the cdr `5`. `match` and the `koskari.matcher` surface refuse an improper
list as a pattern outright. Found by the 2026-11 pre-public review (D11,
ruling R19); recorded for a later investigation, not for now.

---

## Today

- **syntax-rules / macro-rules.** `_syntax_spine_patterns` in
  `koskari/syntax.py` lowers a dotted tail symbol to `REST(name)`, the
  same node a trailing `x ...` lowers to. REST consumes the remaining
  *elements* of a proper spine. An improper subject has no element
  after the dot, only a cdr atom, so the spine walk misses. The tail
  must be a bare pattern variable; a literal, an ellipsis or a nested
  pattern after the dot is refused at definition.
- **match.** `parse_case_element` in `koskari/matcher/case.py` raises
  `improper list is not a valid case pattern` for a pair literal whose
  spine is improper; `koskari/matcher/heads.py` does the same for the
  matcher surface. A subject that is an improper pair can only be
  matched as an opaque value or through `::` type and `(car x)`
  predicates in a guard.
- **Reader.** Source read by the layout reader is always proper. An
  improper subject arises from a hand-written s-expression `(4 . 5)`,
  from runtime `cons` without a nil terminator, or from data a Python
  caller builds.

`docs/guide/macros-and-hygiene.md` documents the dotted pattern as
binding "the remaining forms as a list", so the behaviour matches the
contract. The question is whether the contract should widen.

## Where improper lists matter

- Macros that destructure association-style data `(key . value)` built
  by `cons` at runtime and quoted into a form, or produced by another
  macro's template through a dotted template `(f a . rest)` where `rest`
  is bound to an atom.
- `match` over pair structures from Python callers or from `cons`
  chains: `(cons 1 2)` is a legitimate Koskari value with no pattern
  today.
- Scheme-ported macros, where `(_ a . b)` against `(x . y)` is ordinary.

## What a change would touch

### syntax-rules

A dotted tail that should match the cdr rather than the remaining
elements needs a node that binds "whatever the cdr is": a proper
remainder when the spine continues, the atom when it ends in one. The
matcher's REST already binds the remainder of a proper spine; a
`TAIL(name)` node (or a `REST` flag) would bind the cdr at the current
position whatever its shape. Template transcription of a dotted template
`(f a . rest)` already handles an atom bound to `rest` (the terminator
path in `expand_template_with_hygiene`), so the template side needs no
change. Ownership and repeat bookkeeping (`claim_repeat_variable`) treat
the tail as a plain variable already.

Compatibility: every pattern that matches today keeps matching with the
same binding, since a proper remainder is unchanged. New matches are
improper subjects that miss today.

### match and the matcher surface

A pattern form for an improper spine, for example `[a . rest]` or a
`PAIR`-style head, lowers to element patterns for the proper prefix plus
the same `TAIL` node for the cdr. `koskari/matcher/analysis.py` sizes a
pattern by minimum width; a TAIL has width zero and absorbs any cdr.
`koskari/matcher/emitter.py` emits a MATCH_LOAD of the current cell's
cdr rather than iterating to the end. The analysis needs to know the
spine may end in a non-nil atom so it does not emit the end-of-spine nil
check.

### Docs and corpus

`macros-and-hygiene.md`, `compile-syntax-rules.md`, `match-patterns.md`
and the matcher reference pages would state the cdr rule and show one
improper example each; a corpus record per surface.

## Invasiveness

syntax-rules alone: one new matcher node plus its emitter arm, a change
to the tail lowering in `syntax.py`, tests in `tests/compile/specials/`.
Small. `match`: the pattern grammar in `case.py`, the analysis width and
end check, the emitter arm, and the docs; medium. Neither touches the
reader, the evaluator or the `.kio` format.

## Open

- Whether `rest` in `(m a . rest)` should bind the cdr (Scheme) or stay
  the proper remainder and a *separate* spelling binds the cdr. Binding
  the cdr is the compatible choice since a proper remainder is its own
  cdr.
- Whether `match` wants a dotted bracket form or a named head, given
  that `[a b *: rest]` is the existing spelling for a proper remainder.
