# Koskari Design: `koskari.lint` Advisory Validator

## Status

Proposed design.

## Summary

A new `koskari-extras` package providing a `koskari.lint` module: "flake8 for
Koskari." This is a parallel tool with similar goals to flake8, developed from
scratch against Koskari's own reader and compiler machinery. It does not reuse
the flake8 package.

`koskari.lint` is a reader + validator pair that mirrors the existing reader +
compile pair used for normal execution, but replaces compile-and-eval with a
non-evaluating validate pass. It never runs user code. The goal is a
`reader -> validator` path that applies heuristics, tracks likely module data
against a set of preferences, and raises flags before they bite someone.

The pipeline mirrors `reader -> compile -> eval`:

```
source -> reader (unchanged) -> validate (new) -> structured findings
```

This is a tool shipped to third-party Koskari users, not part of the Koskari
repo's own lifecycle. The authority model is advisory-only: the tool tells you,
it does not stop you. This is consistent with the language's general stance.
`koskari.lint` does not need to be complete to earn its keep: the value is fast,
cheap failure ahead of time, plus opinionated style signal.

## Mandate: a data-driven reporter

This is non-negotiable. The analysis tools are pure data producers. Every check
emits **structured findings** (a stable code, a source span, a message, and any
supporting fields), and never prints, filters, or formats. Configuration plays
no part in the analysis. A finding either fires against the source or it does
not.

A separate `koskari-lint` CLI script owns everything policy-shaped:

1. accept the configuration,
2. run the analysis tools to obtain the findings data,
3. apply the configuration to that data to decide what to output.

"What to output" covers both the **formatting** of results and **which findings
are ignored or overruled**. The CLI is the only component that reads config,
selects and suppresses codes, and renders. Because the product is a stream of
structured findings, the CLI is just one renderer; the LSP integration in
`koskari/extras/lsp` is another, mapping findings straight onto editor
diagnostics.

## Two analysis engines, one frontend

The checks split by the data they need, and no single lossy pass serves both.

* **Form / semantic engine** — sources from the read form tree plus the real
  compile-time resolution and matcher builders. Handles shape, arity, bracket
  placement, and structural rewrites (see use cases). This is the half where
  reusing the compiler is unarguable.
* **Lexical / layout engine** — sources from the token and layout stream (the
  lexer in `koskari/lexer/`, already backing the REPL and LSP). Handles
  whitespace, indentation, blank-line spacing, and docstring placement --
  surface facts the reader deliberately discards, so they must be observed
  before or alongside the read.

```
source ─┬─→ token / layout stream ──→ lexical / layout findings
        └─→ reader → form tree ──→ resolution + matchers → semantic findings
```

Both engines feed the same findings stream consumed by the CLI.

## Configuration model

Linter configuration is first-class and lives entirely in the CLI, fully
independent of `ModulePragma`. Two reasons to keep them apart: the compiler
pragma changes how code *compiles* (for example, the bracket-in-call-position
lint is a real semantic check the compiler enforces), whereas linter opinions
must never change compilation; and a user may want to lint a file to a stricter
standard than it compiles under.

The one coupling kept is read-only awareness: the linter *observes* the pragma
so it does not contradict explicit author intent, and defers rather than
double-reporting where the pragma already governs a behavior.

Severity stays out of the analysis. The engines emit flat findings with no
confidence or severity scoring. Strength lives in configuration: some codes ship
on by default, some are opt-in, and the user promotes any code to an error. A
"STRONGLY RECOMMEND" code is simply one enabled by default in the shipped
profile; a soft suggestion is one the user opts into.

## Example use-cases

Opinionated feedback the tool can give. This is a menu of possibilities, not a
committed v1 scope.

Bracketing and layout:

* **STRONGLY RECOMMEND** certain forms be bracketed with `[]`. The compiler
  pragma intentionally does not offer this positive recommendation.
* **Warn** when bracketed forms appear in a call (function head) position. The
  compiler pragma already supports this second check; the linter can surface it
  too.
* Detect layout misuse that lands a keyword in a call-function position.
* Recommend consistent indentation.
* Recommend spacing (blank lines) between top-level function definitions.

Structure and style:

* Suggest `unless` for `(when (not ...))`, and `when` for `(unless (not ...))`.
* Recommend docstrings on definitions that lack them.
* Flag an obvious arity / shape mismatch against a known transformer's matcher.
* Flag a form that shadows a bootstrap or builtin binding.
* Flag unused `let` bindings and unused imports.
* Suggest collapsing redundant nesting (single-form `begin`, `(begin (begin …))`).
* Flag a `cond` / `case` with an unreachable or duplicated arm.
* Suggest a predicate-style name end in `?` and a mutator in `!`, per convention.
* Flag duplicate keys in a mapping literal.

## Why not a from-scratch parser

The naive version of this idea is "raw parse + reimplement pattern matching
against known forms." That is the wrong shape:

* It duplicates `koskari/matcher.py` / `koskari/hygiene.py` and will drift every
  time those change.
* It cannot correctly handle reader-macro-affected source at all, since reader
  macros are parse-time character dispatch (`set_macro_character`). There is no
  way to know the correct read of downstream forms without actually running the
  real reader with whatever macros were registered.

Instead: reuse the real reader, and reuse the real compile-time pattern
builders. For the known transformer heads (`syntax-rules`, `macro-rules`,
`syntax-match`, `macro-match`, `special-rules`, `special-match`), pattern
construction is already a compile-time, side-effect-free step:
`special_compile_syntax_rules` et al. build the `MatchCompiler` matcher
before anything is evaluated; eval only turns the resulting `build-syntax-rules`
call into a live `Transform` object. So the validator does not need eval at all
for these; it needs the same matcher-construction call the compiler already
makes.

## Design

`validate` walks the same dispatch the compiler does (special vs. macro vs.
call, same resolution/scoping logic), reusing rather than reimplementing that
logic if it is separable from bytecode emission. Real resolution matters even
for style suggestions: rewriting `(when (not x))` to `(unless x)` is only safe
when `when` and `unless` resolve to the bootstrap macros and are not locally
shadowed.

Per-category handling of heads in the form engine:

* **Recognized transformer-defining heads** — call the real compile-time
  pattern builders to get the exact matcher, then validate call sites against it
  exactly. High-confidence, hard-flag category.
* **Undeclared / external / higher-order heads** — presume it is a call, chain
  validation into child expressions, but do not attempt arity/shape checking.
  Report as unchecked rather than guess. This avoids confident false positives
  on by-ref formals, keyword defaults, and `REST` patterns, where "arity" is not
  a count but matcher acceptance.
* **Computed macro specs, or any region following a `set_macro_character` call
  in-file** — mark opaque/skip with a reason code, do not attempt to validate.
  An honest decline costs nothing and preserves trust in the rest of the file.

Every check has a defined opaque/unchecked fallback, and no check ever guesses.
That discipline is what earns a second run of an opinionated tool.

## Coverage / drift concern

If `validate` is a second behavior forked off each `@special` handler, landing a
new special without a matching validate arm silently falls through to "presume
call" (a quiet false negative, not a crash). Worth deciding whether
`@special(...)` registration should carry `validate` as a required sibling of
`compile`, or at least have a coverage test that fails when a special is missing
one.

## Finding categories

The findings the engines emit fall into flat categories, with no confidence or
severity attached at the analysis layer:

* exact mismatch against a known transformer's matcher
* undeclared head (informational)
* opaque/unchecked region (reader macro or computed macro spec)
* style / convention suggestion (bracketing, spacing, naming, docstrings, …)

## Adjacency-significant syntax

A class of mistake the linter is well placed to catch, and which it should
handle as a family rather than one member at a time.

Several reader constructs are **adjacency-significant**: they attach only when
written with no whitespace, and inserting a space produces a different, equally
valid parse rather than an error. The current members:

| Attached | Spaced | What the spaced form is |
|---|---|---|
| `f"{x}"` | `f "{x}"` | symbol `f`, then a string |
| `r"..."`, `b"..."` | `r "..."`, `b "..."` | symbol, then a string |
| `&(form)` | `& (form)` | symbol `&`, then a form |
| `foo[0]` | `foo [0]` | symbol, then a bracket list |
| `foo.bar` | `foo .bar` | symbol, then symbol `.bar` |
| `"a"- " b"` | `"a" - " b"` | two strings around the minus function |

The reader cannot adjudicate any of these -- the spaced reading is legitimate
in each case. Passing a string, the minus function, and another string as
three arguments is a real thing to write.

**How loudly the mistake fails is luck, not design.** Most of these blow up
because the stray prefix symbol is unbound, so `f "{x}"` usually reaches a
`NameError`. But `-` *is* bound, and string literals are self-evaluating, so
`("a" - " b")` at top level quietly evaluates to `" b"`. `(f "{x}")` where `f`
happens to be a function is equally quiet. There is no rule separating the
loud cases from the silent ones, which is why detection has to be
pattern-based rather than resolution-based.

**Two mitigations at two layers, and the linter is the second.** The uniform
fix is the lexer: if an attached prefix tokenized as one unit and a spaced one
as two, the difference would be visible while typing, across every member at
once and including the bound-`f` case. The linter is the backstop for source
not being read in a highlighting buffer, and for review.

The finding belongs in the **style / convention** category and must be a
suggestion, never an error. Suggested shapes, both narrow enough to have
near-zero false positives:

* a bare `-` symbol sitting between two adjacent string literals in a body
* a bare `f`, `r`, or `b` symbol immediately preceding a string literal in an
  argument list

Both are cheap to spot in read output and neither can plausibly be intended.
A broader rule -- flagging every spaced occurrence -- would be noise, since
each spaced form is ordinary syntax somewhere.

New members will arrive as suffix reader macros are added (see
[archive/suffix-macros.md](archive/suffix-macros.md)), so this wants to be a table the linter
consults, not a set of hand-written checks.

## Open questions

1. Is compiler resolution logic (special vs. macro vs. call dispatch, scoping,
   `request_global`) separable from bytecode emission, so the validator can call
   it directly instead of reimplementing dispatch?
2. Should `validate` be a required sibling of `compile` on `@special`
   registration, to catch missing coverage as new specials land?
3. What is the linter's own configuration format and precedence, and how does it
   read (observe, never share) the module pragma?

# The end.
