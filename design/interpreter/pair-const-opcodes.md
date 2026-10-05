# Pair opcodes fed from the constant pool

## Status

Idea, recorded 2026-10-01 while adding `BRACKET_CONS` and
`BRACKET_PROPER`. Not implemented, not measured.

## Today

A quoted pair, `(quote (a b c))`, is requoted into
`(cons (quote a) (quote b) (quote c) nil)` and compiles to one `CONST`
per element and a `CONS n`; a quasiquote segment with no splice does
the same with `PROPER n` or `BRACKET_PROPER n`. The constant pool holds
tuples but not pairs, so a list of constants cannot be one `CONST`,
while a quasiquote segment that feeds a `PAIR_SPLICE` already rides as
one `CONST` tuple, which `PAIR_SPLICE` reads directly
(`compile/specials/quasiquote.py`, `_emit_segment`).

## Idea

Four opcodes that take their elements from a pool tuple instead of the
stack, so a quoted or quasiquoted list renders down over its constant
elements in one instruction:

- **`PROPER_CONST`** and **`BRACKET_PROPER_CONST`**, CONST style: the
  arg names a pool tuple; push the proper list, plain or bracket, of its
  elements. `(quote (a b c))` is one row; a quasiquote whose every
  element is constant is one row.
- **`CONS_CONST`** and **`BRACKET_CONS_CONST`**, EXTRA style, a pool
  tuple and a count: the count names stacked values that lead the chain,
  the tuple supplies the elements after them with its last element as
  the tail, and the chain is built in one op without the constant run
  ever touching the stack. `` `(,x b c) `` is one `VAR x` and one row;
  `` `(a ,x . d) `` needs the constant on both sides and stays as it is.

The element order in the tuple is the chain order, so the handlers are
`KosPair_ConsFromPtr` and its bracket twin over the tuple's item array,
with the stacked prefix copied ahead of it or consed on afterwards.

## What it would change

- `requote` in `compile/helpers.py` stops rewriting a constant-only
  spine into a cons form and emits the one row; a spine with a
  non-constant element keeps the rewrite.
- `_emit_segment` emits `PROPER_CONST` or its bracket twin for an
  all-constant segment that is not a splice feeder, and `CONS_CONST` for
  a segment whose dynamic elements all lead.
- The processor's const-folding of a pair-building run is the same
  judgement the quasiquote makes at emission; one of the two should own
  it.

## To settle

- Whether the constant-only quote should instead become a pool pair
  kind, one `CONST` with no build at all, and what that costs the codex
  and the mutability rule for quoted data.
- Measurement: the rows save instructions on every quoted list a macro
  expands, which is where templates spend; the gain is the dispatch and
  stack traffic of n `CONST` rows, measured by the trace counters
  before it is taken.

## How to choose

Write a set of demo functions that use `quote` and `quasiquote` in the
shapes templates produce: a constant-only list, a bracket list, a
dynamic head with a constant tail, a constant head with a dynamic tail,
a dotted tail, nested lists, and a splice between constant runs. Take
their opgrams (`tox -qe opgrams`, the emission counts the perf tooling
reads) and tally which runs of `CONST` rows into `CONS`, `PROPER` and
their bracket twins recur. The rows worth adding are the ones that
collapse the most frequent runs; a shape the demos never produce gets
no opcode.

## Related

- `koskari/lib/opcode/CONS.c`, `PROPER.c`, `BRACKET_CONS.c`,
  `BRACKET_PROPER.c`, `PAIR_SPLICE.c`.
- `koskari/compile/specials/quasiquote.py`, `koskari/compile/helpers.py`
  (`requote`).
- [opcode-sizing.md](opcode-sizing.md) for measuring a new row.
