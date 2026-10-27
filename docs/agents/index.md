# Koskari orientation for AI agents

Koskari is a Lisp dialect for Python: `.ki` source files combine an
indentation-based layout syntax with s-expressions, expanded through
macros and compiled to bytecode that runs alongside ordinary Python.

Before generating any Koskari source, read
[Writing Koskari source files](writing-ki-files.md) for the parse-level
rules, then [Pitfalls](pitfalls.md) for the semantic traps that follow
from Python or Scheme priors.

---

## Which page answers what

- Every name bound by default -- layout handlers, special forms, macros,
  fences, runtime types, operators, functions, exceptions -- each linked
  to its reference page:
  [Builtin names for agents](builtins.md)
- Formal grammar -- the normative catalogue of reader syntax; there is
  no separate BNF document, this page is its equivalent:
  [Grammar](../reference/grammar.md)
- Layout and indentation mechanics, normative rules:
  [Reader and layout syntax](../reference/reader-layout.md)
- Why layout and s-expressions coexist and when to use which:
  [Koskari and s-expressions](../guide/layout-and-s-expressions.md)
- The call convention, and `[ ]` versus `( )`:
  [Brackets and the call convention](../guide/brackets-and-call-convention.md)
- Idiomatic style -- prefer the direct form:
  [Idiomatic Koskari](../guide/idiomatic-koskari.md)
- Gentler on-ramp for Python-shaped expectations:
  [Layout and calls](../tutorials/from-python/layout-and-calls.md)
- Python and Scheme reflexes that misbehave in Koskari, as reflex /
  result / fix tables: the native `map`, `filter`, and `partial`, when
  `demand` wrapping applies, `koskari.itertools` rather than
  `itertools`, pair chains, special forms in head position, and which
  exception class to catch:
  [Python reflexes versus Koskari behaviour](native-builtins.md)
- Exception names at the read, compile, and run phases:
  [Error guide](../reference/error-codes.md)

## Querying this corpus

The verified example corpus and these documentation pages are served by
the same tools. [Querying the corpus](querying-the-corpus.md) covers
the search flow, feature paths, valid/invalid polarity, and the tag
vocabulary. The fastest loop is a search, then `get_reference` on the
returned path.
