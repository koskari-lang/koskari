# User documentation goals

This document is **meta-documentation**: it describes what **future,
human-facing user documentation** should contain, how it should be structured,
and how it relates to material under `design/`. It is intended to be revised
collaboratively, then used as a prompt (or checklist) when bootstrapping a
top-level `docs/` tree or an external documentation site.

**Not in scope here:** implementation of that tree, tooling (MkDocs, Sphinx,
static site generator), or CI. **In scope:** audiences, pedagogy, information
architecture, maintenance rules, and coupling to existing authoritative sources
(opcodes, bytecode, syntax).

---

## 1. Purpose and separation from `design/`

The `design/` directory holds **working notes, invariants, and reference
extracts** aimed at maintainers and at automated or semi-automated agents. It is
**not** optimized for a person learning Koskari or Sibilant from scratch.

Future user documentation must:

- Teach **skills and mental models** in a deliberate order.
- Provide **stable entry points** (install, first program, glossary).
- Offer **neutral reference** pages that are easy to search and deep-link.
- State **stability** (stable, evolving, experimental) where behavior may change.

Cross-references from user docs into `design/` are acceptable for contributors
or for “read the contract” links, but **learners should not need `design/` to
complete the primary tutorials.**

---

## 2. Audiences and on-ramps

User documentation assumes **two primary on-ramps**, which fork from a tiny
shared introduction and later merge into a **shared concept spine**.

### 2.1 On-ramp A: new to programming

Readers may have no prior language. Documentation should:

- Prioritize **concrete outcomes** (edit a literal, run, see output) before
  abstract vocabulary.
- Define jargon **in place** on first use (or link to a single glossary).
- Teach **reading error messages** as a core skill early.
- Avoid opening with comparisons to other languages.
- Keep the **early standard library surface** small and explicit; defer
  advanced features to later chapters.

### 2.2 On-ramp B: familiar with Python

Readers bring habits and expectations from Python. Documentation should:

- Provide a **Rosetta-style mapping** (Python construct → Koskari shape → why
  it differs when it does).
- Call out **migration traps** explicitly (truthiness, mutability, scoping,
  iteration, modules, errors, async model, etc.).
- Separate **tooling** (how to run Koskari vs `python`, projects, tests) from
  **language semantics**.
- Include **interop** guidance when applicable (embedding, calling Python, or
  constraints), or a clear “not available yet” with a pointer to roadmap or
  issues.

### 2.3 Shared spine

Both on-ramps must converge on **one evaluation story** and **one set of
reference pages**. Two narratives must not contradict each other. Shared
material lives under a neutral `guide/` (or equivalent) hierarchy after the
fork.

---

## 3. Pedagogical approach

User docs should **lean into** the methodology exemplified by *The Little
Schemer* (build from a tiny substrate, repetition with variation, complexity
as consequence of earlier rules) **without** copying its voice, dialogue
format, or presentation.

Practical techniques:

- **Invariant-first:** state the one rule the reader must not forget, then
  vary examples around it.
- **Predict-then-reveal:** short “what should this print?” moments before
  showing results.
- **One new idea per section** where possible; split or defer secondary ideas.
- **Explicit “not yet”:** when a feature resembles Python or Scheme, say what
  Koskari does now and what is out of scope or deferred.
- **Failure as curriculum:** common mistakes and error patterns beside happy
  paths.

Optional exercises improve retention; they can live in the main trail or in a
separate `exercises/` area.

---

## 4. Information architecture (layout index)

Path prefixes below are **logical**; the actual root may be `docs/` on disk or
a site URL. Each subsection has a **two-line summary** of intended contents.

### 4.1 `start/` — First contact

| Section | Summary |
|--------|---------|
| `start/overview.md` | What Koskari is for, prerequisites, and how the site is organized (two on-ramps, then shared material). Sets expectations: tutorial vs reference, stability labels, where to get help. |
| `start/install-run.md` | Install Sibilant (or the canonical runner), verify with one command, run a minimal file or REPL. Stops at “it works”; no full language semantics. |

### 4.2 `start/beginner/` — New to programming

| Section | Summary |
|--------|---------|
| `start/beginner/00-hello-change.md` | Run a tiny program; change a literal; observe new output. Introduces the edit-run-read loop. |
| `start/beginner/01-words-and-punctuation.md` | How source text is structured: names, numbers, delimiters, strict reading. Optional micro-glossary tied to real error locations. |
| `start/beginner/02-calling-things.md` | “Do this with these inputs” as the primary action; minimal examples; formal vocabulary only when natural. |
| `start/beginner/03-results-and-messages.md` | Normal output vs errors; how to read the first useful line of a message. Debugging as a basic literacy. |
| `start/beginner/04-repeating-patterns.md` | One controlled pattern (nesting, lists, or a single looping form—curriculum-specific). Stops before the full language. |
| `start/beginner/where-next.md` | Bridge into the shared guide; links to glossary and the first shared concept chapter. |

### 4.3 `start/from-python/` — Python background

| Section | Summary |
|--------|---------|
| `start/from-python/map.md` | Rosetta overview: Python ↔ Koskari with “transfers” vs “misleads” habits. |
| `start/from-python/runtime-and-tooling.md` | Running Koskari vs `python`, venv, project layout; links to the same install-verify path as beginners, framed for migration. |
| `start/from-python/data-and-control.md` | Booleans, numbers, strings, collections, conditionals, iteration—tight mappings; truthiness, mutability, order of evaluation only where they diverge. |
| `start/from-python/functions-and-scope.md` | Definitions, arguments, returns, closures (if applicable), visibility; contrasts with Python’s model where needed. |
| `start/from-python/errors-and-debugging.md` | Failure modes and debugging workflow vs typical Python practice. |
| `start/from-python/interop.md` | Calling Python from Koskari or embedding, with constraints—or explicit “not yet” plus pointer to roadmap. |
| `start/from-python/where-next.md` | Merge into shared guide; suggests skim vs deep reading by background; links toward patterns/cookbook when appropriate. |

### 4.4 `guide/` — Shared concept spine

| Section | Summary |
|--------|---------|
| `guide/00-model-of-evaluation.md` | Short accurate story: what is read, what is evaluated, what is data. Ends with prediction-friendly micro-examples. |
| `guide/01-forms-and-combining.md` | How programs compose; nesting and grouping; connects surface syntax to executable structure without dumping the full grammar. |
| `guide/02-bindings-and-environments.md` | Names, definitions, shadowing, persistence across calls; optional step-by-step traces. |
| `guide/03-functions-deep.md` | Recursion, higher-order functions, idiomatic functional patterns; convergence point for both on-ramps. |
| `guide/04-data-structures.md` | First-class structures and traversal; sidebar for common wrong mental models from Python or Lisp if useful. |
| `guide/05-control-flow.md` | Real conditionals and iteration style; avoids cataloguing features outside the recommended teaching path. |
| `guide/06-modules-and-program-structure.md` | Files, imports, project layout aligned with how the implementation and tests expect code to be organized. |
| `guide/07-macros-or-syntax-extension.md` | If applicable: what macros solve, safety/hygiene at a user level, minimal examples. Omit or replace if not user-facing yet. |
| `guide/08-types-or-contracts.md` | If applicable: types, predicates, contracts beyond dynamic defaults. Otherwise “not yet” and optional link to design notes. |
| `guide/09-io-and-effects.md` | Printing, files, time, environment—supported stdlib surface; separates pure core from effects. |
| `guide/10-testing-and-quality.md` | How tests are written and run in this ecosystem (e.g. tox, project conventions). |

#### 4.4.1 Optional guide material (bytecode curiosity)

Not required for either on-ramp; links from “contributors” or “curious readers”
only.

| Section | Summary |
|--------|---------|
| `guide/bytecode-mental-model.md` | Why bytecode exists; stack-machine intuition; when ordinary use needs this vs debugging vs contributing. |
| `guide/from-source-to-stack.md` | Trace one or two tiny programs from syntax to a short opcode sequence; trains prediction without full compiler detail. |

### 4.5 `reference/` — Neutral lookup

| Section | Summary |
|--------|---------|
| `reference/grammar.md` | Syntax reference with canonical examples; ambiguity notes; stable anchors for tooling links. |
| `reference/reader-layout.md` | Whitespace, comments, continuations, reader-level rules—keeps “text → structure” out of tutorial voice. |
| `reference/special-forms.md` | Special forms: signatures, evaluation rules, restrictions, examples; structured for search. |
| `reference/stdlib/` | One page per module or topic: procedures, arity, errors, edge cases; links to guide for rationale. |
| `reference/cli-and-config.md` | Flags, config, environment variables, exit codes; mirrors install doc but exhaustive. |
| `reference/glossary.md` | Shared definitions; every jargon term in tutorials should link here on first use. |
| `reference/error-codes.md` | Stable error identifiers or message patterns with fixes; link targets for REPL and logs. |

### 4.6 `reference/vm/` — Opcodes and bytecode (user-visible reference)

This tier **represents the instruction set** in documentation aimed at
implementers, advanced debuggers, and contributors. It must stay aligned with
`koskari/lib/opcodes.incl` and with authoritative semantics.

Relationship to `design/OPCODES.md`:

- **`design/OPCODES.md`** remains the **implementation contract** today
  (stack effects, invariants, compiler notes).
- **User-facing** pages under `reference/vm/` should either **mirror** that
  contract in a tone suitable for public docs or **summarize** with deep links
  into `design/` where detail is overwhelming. **Do not maintain three divergent
  opcode lists**; CI or process should detect drift from `opcodes.incl`.

| Section | Summary |
|--------|---------|
| `reference/vm/overview.md` | Role of `KosCode`, instruction layout, constant pool, positions, flags; how to obtain disassembly (`koskari.dis` or equivalent). |
| `reference/vm/bytecode-format.md` | Encoding: `(op, arg)`, `EXTRA` rows, metadata tables, versioning notes if any. |
| `reference/vm/opcode-index.md` | Index of `KOS_OP_*` names with one-line intent and anchor links; ideally generated or verified against `opcodes.incl`. |
| `reference/vm/opcodes.md` | Per-opcode entries: stack effect, argument kind, invariants, typical compiler patterns; public counterpart to `design/OPCODES.md`. May be split by family. |
| `reference/vm/opcode-families.md` | Narrative grouping (stack, constants, calls, scope, control, exceptions, dyn blocks, async, continuations, etc.) matching how opcodes are mentally organized. |
| `reference/vm/disassembly-and-debugging.md` | How to read dumps, labels, positions; relate traces and crashes to instruction indices. |
| `reference/vm/peephole-and-lowering-notes.md` | Optional: documented `Compiler.compress`, fast-var lowering, or other transforms that affect observable bytecode. |

### 4.7 `patterns/` — Cookbook

| Section | Summary |
|--------|---------|
| `patterns/index.md` | How to use recipes; prerequisites per recipe; links back to `guide/`. |
| `patterns/recipes/*.md` | Task-oriented pages: parse a file, walk a tree, small DSLs, tests, macros—problem, minimal solution, variants, pitfalls. |

### 4.8 `project/` — Contributors and implementation map

| Section | Summary |
|--------|---------|
| `project/build-test.md` | Build, local tests, CI interpretation (e.g. tox, Makefile); constraints from repo policy. |
| `project/architecture-map.md` | Lexer, reader, compiler, VM, importer—where to read code; invariants at a glance. May point to `design/` as internal notes. |
| `project/vm-and-opcodes.md` | Process: add opcode in `opcodes.incl` first; update `reference/vm` and `design/OPCODES.md` per policy; PR checklist. |
| `project/release-and-versioning.md` | Versioning, compatibility, changelog; helps Python-adjacent users plan upgrades. |

### 4.9 Cross-cutting pages

| Section | Summary |
|--------|---------|
| `changelog.md` or `news.md` | Release notes; migration links when behavior changes. |
| `faq.md` | Editor support, platforms, performance expectations—keeps tutorials free of defensive asides. |

---

## 5. Minimum viable documentation (v1) suggestion

To ship something useful without boiling the ocean:

- Both `start/` tracks through `where-next.md`.
- `guide/` through **modules and program structure** plus **testing and
  quality** (or a minimal testing page).
- `reference/glossary.md`, `reference/special-forms.md` (even if initially
  partial), `reference/cli-and-config.md`.
- `reference/vm/overview.md` plus a clear pointer to full opcode detail
  (`reference/vm/opcodes.md` or `design/OPCODES.md` until merged).
- `patterns/` with a **small** set of high-value recipes (three to five).

Expand `reference/stdlib/` and full `reference/vm/opcodes.md` as usage
surfaces gaps.

---

## 6. Maintenance and single sources of truth

- **Opcodes:** `koskari/lib/opcodes.incl` defines order and membership; opcode
  **semantics** must have one authoritative narrative (today `design/OPCODES.md`;
  tomorrow possibly `reference/vm/opcodes.md` with design linking back).
- **Syntax:** `reference/grammar.md` and `reference/reader-layout.md` should
  align with tests and with `design/SYNTAX.md` (or supersede it for public
  wording while design stays the scratchpad).
- **Glossary:** one file; tutorials link in instead of redefining terms.
- **Stability labels** on pages or sections reduce surprise when the language
  or VM still moves quickly.

---

## 7. Bootstrap note

When creating a physical `docs/` directory (or external site repo), treat this
file as the **spec**: copy the layout index into the nav structure, file
stubs, or a tracking checklist. Revision history of **this** meta-document
belongs in git with the rest of `design/`; the eventual user doc site may
version its own content on release tags.

---

## 8. See also

- [OPCODES.md](OPCODES.md) — opcode semantics and invariants (implementer-focused).
- [interpreter/bytecode.md](interpreter/bytecode.md) — bytecode format and
  layout.
- [SYNTAX.md](SYNTAX.md) — syntax design reference (not learner-optimized).
