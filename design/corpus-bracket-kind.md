# Corpus read and expand expectations and bracket kind

The corpus harness cannot see bracket kind, so read and expand
expectations that record the wrong bracket style pass. This note
describes the gap and a fix to take up when corpus consistency becomes
a goal in its own right. Found by the 2026-11 pre-public review (D14).

---

## The gap

The reader records bracket style: `'[1 2 3]` reads as a `bracket-pair`
list and `'(1 2 3)` as a `pair` list, and the two print differently.
They are value-equal under `equal?` and under Python `==`, by design:
pair equality compares cars and cdrs and treats the two nil flavours as
the same list end (pair.c, `pair_eq`).

The harness in `tests/corpus/__init__.py` compares a record's
`verify.read` or `verify.expand` text to the form the implementation
produced through `form_to_py`, which turns either pair kind into a
Python list and passes either nil through. `forms_equal` therefore
accepts `(class Token () ...)` for a source that reads as
`(class Token [] ...)`, although its docstring promises exact
structural equality.

Consequences:

- About 77 records (132 read and expand expectations) carry the wrong
  bracket style and pass. Most are layout forms (`class X []:` recorded
  as `()`), plus with, dyn-with, for-each, parameterize, define-values
  and yield cases. The two `iter-reader-bracket-*` records' read strings
  are among them.
- The read and expand stages disagree with the eval stage, whose
  `bracket-list:` encoding (docs/corpus/SCHEMA.md) records bracket kind
  as a fact.
- An agent reading a stale expectation learns that the reader turns
  brackets into parens. For compiled forms that is harmless, since
  special-form handlers test pair and nil only; for quoted data it is
  wrong, since the runtime value of `'[1 2 3]` prints as `[1 2 3]`.

The two record notes that claimed brackets and parens produce
"identical cons pairs" were corrected on 2026-11-04 to say value-equal,
different types. The expectations themselves were left as they are.

## Why `form_to_py` exists

Readable mismatch output and the `...` wildcard matching for the
expand stage. Neither needs a Python conversion: the forms' own repr
shows brackets, and the wildcard walk can run over the forms. A plain
`got == exp` would not help, since pair equality is bracket-blind.

## Fix proposal

1. Replace the `form_to_py` comparisons with a strict structural walk
   over the forms: identical types at every node (pair versus
   bracket-pair, nil versus bracket nil), symbols by name, leaves by
   identical type and equality so `1` never passes for `True`.
   `forms_equal` becomes that walk, `form_matches_pattern` the same
   walk with the `...` wildcard, and `eval_values_equal`'s improper-pair
   branch uses it too. `form_to_py` and the Pythonized pattern trees go
   away.
2. The quick tool's round-trip check (`tools/corpus/quick.ki`,
   `_stage_read` and `_stage_expand`) already calls `forms_equal`, so
   its emitted strings become strict with no change.
3. Run the corpus; the stale expectations go red. Regenerate each
   through `quick verify --read` or `--expand` on the record's source,
   and check every regenerated line against the brackets written in that
   record's own source. A record whose reader output differs in bracket
   style from its source is a finding to report, not to record.

No koskari/ code changes. Cost: a comparator of about forty lines, a
mechanical regeneration of 132 lines, and a review pass.

## Related

- [corpus-schema-philosophy.md](corpus-schema-philosophy.md)
- docs/corpus/SCHEMA.md (eval encodings, `bracket-list:`)
- docs/reference/grammar.md (bracket style is recorded by the reader)
