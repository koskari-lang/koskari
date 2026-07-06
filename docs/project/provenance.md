# Development and provenance

Koskari is developed with substantial help from AI coding agents, and this page
states plainly how that works. The project treats the practice as a matter of
transparency rather than something to obscure.

---

## The short version

Koskari is designed, reviewed, and maintained by a human author. AI coding
agents -- across more than one tool (Cursor, Claude Code) and several
models -- are used as leverage: to draft changes, generate boilerplate, and
help maintain supporting material. Every contribution is read, judged, and
accepted or rejected by the author. Nothing is merged on faith.

Agent involvement is **not** tracked per file. Earlier revisions carried a
per-module `:ai-assistant:` field, but that field drifted into a dozen stale,
inconsistent values and claimed a precision it never actually had. It has been
removed. The authoritative record of who changed what, and when, is the git
history.

---

## Where the work is human, and where agents help

The **core of the language is human-crafted**: the evaluator and its virtual
machine, delimited continuations, dynamic wind/unwind, macro hygiene, and the
compiler semantics. These carry invariants that have to be held in one head to
be correct, and they are.

Agents contribute most in the areas around that core:

- Editor support and the LSP.
- Large portions of the unit-test suite (always under review, because tests are
  what assert the core is correct).
- Upkeep of internal design documentation.
- Boilerplate and mechanical refactors.

The dividing line is deliberate. Judgment over what defines correct behavior --
the language semantics and the tests that guard them -- stays with the author.

---

## Why disclose this

Two reasons. First, honesty: readers deserve an accurate picture of how the
software they are evaluating was built. Second, calibration: Koskari is an early
`0.x` project (see [release-and-versioning](release-and-versioning.md)), and
knowing how it is produced is part of judging what to rely on. The disclosure is
a statement of practice, not a limitation of liability.
