# Documentation reorganization proposal

A findings-and-plan document for restructuring the user-facing `docs/` tree.
It builds on [documentation-goals.md](documentation-goals.md) (the original
information-architecture spec) and the live status in
[archive/docs-tracker.md](archive/docs-tracker.md), and records decisions taken during the
2026-07 documentation audit.

**Scope:** organization, page roles, reading path, and tone — *not* per-page
rewrites. This document is the spec a subsequent authoring pass works from.

---

## 1. The governing idea: one path, ever deeper

Documentation is a single trail that descends through a **hierarchy of needs**.
Each tier has a distinct audience, phrasing, and tone, and the reader moves
down only as far as their need requires — but the path is continuous, never a
set of disconnected silos.

| Tier | Reader | Need | Tone |
|------|--------|------|------|
| **Beginner** | New to programming | Get their feet under them | Little-Schemer emulation: tiny substrate, repetition with variation, predict-then-reveal. Concrete before abstract. |
| **From Python** | Fluent in Python | Map what they know, then see past it | Comparative Rosetta examples first; then deliberately move *beyond the shackles of Python* — macros, continuations, the async substrate. |
| **Guide** | Writing Koskari, wants orientation | "Is this concept relevant to my use case, or something to revisit later?" | TL;DR glance. Fast enough to skim and decide. Ends by pointing deeper. |
| **Concepts** | Committed, needs the real rules | The authoritative, final word | Exhaustive. May be *more* than the reader wanted — that is correct; it is the reference of last resort. |
| **Reference** | Knows the form name, wants exact behavior | Lookup | Neutral, searchable, deep-linkable. Generated where possible. |

The two on-ramps (beginner, from-python) converge on the **guide**; the guide
diverts, per topic, into **concepts**; concepts and reference are the floor.
This preserves the existing "two on-ramps → shared spine" architecture and
extends it downward with an explicit depth gradient.

---

## 2. Findings from the audit

### 2.1 The nav spine is sound

All 16 index files parse; **zero dangling `{toctree}` entries**. This is a
reorganization of *roles and ordering*, not a repair of broken navigation.

### 2.2 `reference/concepts/` is emergent drift

The original IA ([documentation-goals.md](documentation-goals.md) §4) has **no
concepts tier**: guide/ is the conceptual spine, reference/ is neutral lookup.
`reference/concepts/` (6 pages) was added later and now duplicates guide topics:

| Topic | Guide page | Concepts page |
|-------|-----------|---------------|
| Bindings / refs | `02-bindings-and-environments`, `references-and-storage-indirection` | `bindings-and-refs` |
| Layout vs s-expr | folded into `00`/`01` | `layout-and-s-expressions` |
| Async / demand / defer | folded into `05-control-flow` | `demand-async-and-defer` |
| Macro hygiene | `07-macros-or-syntax-extension` | `macro-matching-and-hygiene` |
| Brackets / call convention | folded into `01-forms-and-combining` | `brackets-and-call-convention` |
| Pragmas | (none) | `pragma` |

A reader who meets "bindings" in two places cannot tell which to read, and an
author has no rule for where new conceptual prose belongs. **This is the
central problem.** Resolution: **A2** — keep both tiers but assign each a
distinct role (guide = glance, concepts = authoritative), and wire an explicit
divert from every guide chapter that has a concepts counterpart.

### 2.3 The guide chapters already fit the "glance" model — with one outlier

Numbered chapters are uniformly TL;DR-sized (~200–340 words). The exception is
`guide/references-and-storage-indirection.md` at **1544 words** (5–7× any
chapter) — an unnumbered deep-dive that landed in the guide, with **no concepts
page of its own**. The A2 model is literally inverted here: the authoritative
depth lives in the guide.

### 2.4 A2 diverts barely exist yet

Only chapters `00` and `01` link into `concepts/` at all. Reframing each guide
chapter as glance-plus-divert is real authoring work, not just file moves.

### 2.5 Guide numbering encodes an order that drifted

The guide was numbered `00–10` per the spec, then features grew and unnumbered
pages (`references-and-storage-indirection`, `bytecode-mental-model`,
`from-source-to-stack`) were interleaved/appended. The integer prefixes now
*promise* a rigid sequence the contents no longer honor.

### 2.6 Empty pages are generated reference stubs, not authored pages

No guide/start page is empty. The empties are auto-generated symbol-per-page
reference stubs — `reference/macros/*` (many at 2 content lines: `compile-if`,
`compile-when`, `compile-unless`, `compile-when-main`, …) and
`reference/vm/opcodes/*`.

**Provenance (per project owner):** almost all of `reference/` is *100%
agent-generated* as extractions from the original `design/` rollups, whose
purpose is to bootstrap agent understanding of project state. Every special or
macro page *could* carry an in-depth usage guide (how to use it, what to
expect, links to related forms) — but **most do not warrant one.** So this is
not "fill every stub"; it is a *selective enrichment* concern, handled
separately from the narrative reorg (see §5).

### 2.7 Embedding/hosting has no prose home

The only "host Koskari in a Python app" guidance is `from-python/where-next`
pointing at `api/` autodoc stubs (correctly excluded from the corpus). No
narrative page exists in start/guide/patterns. Missing content home, not a
thin page.

---

## 3. Proposed changes

Ordered smallest-blast-radius first. Each is independently shippable.

### C1 — Drop guide integer prefixes; order via toctree

Rename `guide/NN-*.md` → `guide/*.md`; the reading order lives in the
`guide/index.md` toctree, not in filenames. Removes the false promise of a
rigid sequence and lets new chapters slot in by meaning. (Requires updating
inbound links; grep shows guide chapters are linked from `where-next` pages and
each other.)

### C2 — Re-layer the guide toctree into explicit groups

Split the flat toctree into labelled sub-sections:

- **Core** — the evaluation-model → program-structure teaching sequence.
- **Internals (optional)** — `bytecode-mental-model`, `from-source-to-stack`,
  flagged "not required for either on-ramp," per goals §4.4.1.

### C3 — Resolve the `references-and-storage-indirection` inversion

Move its authoritative depth to a new
`reference/concepts/references-and-storage-indirection.md`; leave a short
glance in the guide (`bindings-and-environments`, or a sibling) that diverts to
it. Restores the guide=glance / concepts=authoritative invariant for the one
page that most violates it.

### C4 — Wire A2 diverts

Give every guide chapter with a concepts counterpart a consistent closing
divert ("Go deeper: [concepts/X]"). Mapping in §4. Guide chapters *without* a
concepts page (data-structures, modules, io-and-effects, testing) divert to the
relevant reference symbol pages or carry no divert — stated explicitly so the
absence is intentional, not an oversight.

### C5 — Practitioner decision aids (new concepts pages)

The practitioner tier's need is "which construct for *my* case?" These are
decision-first pages, not narrative. First target, drawn directly from the
async substrate:

- **"Choosing an async construct"** — `demand` vs `defer` vs `as-coroutine`
  vs `coroutine-fn`: a decision table plus one worked example each. Sits in
  `concepts/` beside `demand-async-and-defer` (which becomes its deep-dive
  backing).

### C6 — Give embedding a prose home

One authored page — likely `patterns/recipes/embedding-host.md` — covering the
importer + `run_file_async` + asyncio-host story, with `from-python/where-next`
pointing there instead of at autodoc.

### C7 — Reconcile the tracker

`docs-tracker.md` still marks written pages as "Stub" and omits the concepts
tier entirely. Update it to reflect reality and to record the concepts tier as
a sanctioned part of the IA (fold this proposal's model back into
documentation-goals.md §4 once accepted).

---

## 4. Guide → concepts divert map (for C4)

| Guide chapter (glance) | Diverts to (authoritative) |
|------------------------|----------------------------|
| model-of-evaluation | `concepts/layout-and-s-expressions`, `concepts/demand-async-and-defer` |
| forms-and-combining | `concepts/layout-and-s-expressions`, `concepts/brackets-and-call-convention` |
| bindings-and-environments | `concepts/bindings-and-refs`, `concepts/references-and-storage-indirection` (new, C3) |
| functions-deep | `concepts/demand-async-and-defer` (coroutine-fn), "choosing an async construct" (new, C5) |
| control-flow | `concepts/demand-async-and-defer` |
| macros-or-syntax-extension | `concepts/macro-matching-and-hygiene` |
| data-structures | reference symbol pages (no concepts page — intentional) |
| modules-and-program-structure | reference import pages (no concepts page — intentional) |
| types-or-contracts | design notes / reference (thin — drop candidate, §6) |
| io-and-effects | reference (thin — drop candidate, §6) |
| testing-and-quality | `project/build-test` (overlap flagged in tracker) |

Concepts pages with no guide counterpart (`pragma`) are practitioner-tier
lookup reached from reference and from decision aids, not from a guide glance.

---

## 5. Reference-stub enrichment (separate track) — DECIDED: enrich at source

The 2-line `reference/macros/*` and `reference/vm/opcodes/*` pages are
agent-generated extractions from the `design/` rollups (see §2.6); the tooling
emits near-empty pages when a rollup entry has no narrative. **Decision: enrich
at the source.** But — per §2.6 — enrichment is *selective*, not blanket:

- **Not every stub is a defect.** A form whose behavior is obvious from its
  signature does not need a hand-authored guide; a 2-line page is acceptable
  there. Target enrichment at forms that are *used often* or *easy to misuse*.
- **Source of enrichment = design rollups + the corpus, not hand-edits.** The
  corpus already carries verified canonical examples keyed by feature
  (`macro/compile/*`, `special/compile/*`, …). The highest-leverage enrichment
  is to have the generator inject the canonical corpus example(s) for a form
  into its reference page, plus a one-line semantics summary from the rollup.
  This keeps examples verified and aligns with the corpus-first rule
  ([documentation-goals.md](documentation-goals.md) §6).
- Changes must survive `tox -e regen-docs-ref`; **do not hand-edit generated
  pages.** Enrichment lives in the generator inputs (rollups, corpus) and the
  split tooling.

Suppressing empties (dropping sub-threshold pages from the glob toctree)
remains a fallback for forms not worth an example, but is secondary to
corpus-injection.

---

## 6. Decisions taken (2026-07 audit)

1. **Thin guide chapters** (`types-or-contracts`, `io-and-effects`,
   `testing-and-quality`) — **decide per-chapter later.** Judged individually
   when the authoring pass reaches each, against whether it advances "ever
   deeper" rather than padding the count. Not resolved now.
2. **Reference-stub policy** — **enrich at source, selectively** (§5). Corpus
   example injection + rollup summary for high-value forms; leave obvious forms
   thin; suppression is a fallback.
3. **Embedding page home** — **`patterns/recipes/embedding-host.md`** (C6). A
   task-oriented recipe, not a guide chapter.

---

## 7. See also

- [documentation-goals.md](documentation-goals.md) — original IA spec; update
  §4 to sanction the concepts tier once this is accepted.
- [archive/docs-tracker.md](archive/docs-tracker.md) — live status; stale, reconcile per C7.
- [corpus-schema-philosophy.md](corpus-schema-philosophy.md) — prefer
  corpus-backed examples in any authored page.
