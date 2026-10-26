# Documentation tracker

Living status for the user documentation site under `docs/`, correlated with
[documentation-goals.md](../documentation-goals.md) and
[corpus-schema-philosophy.md](../corpus-schema-philosophy.md).

Build: `make docs` or `tox -e sphinx` (output: `build/docs/`).

Regenerate reference splits: `tox -e regen-docs-ref` (overwrites generated
reference pages and bootstrap scripts output).

---

## Documentation reorganization (2026-07)

The reorg in [docs-reorg.md](../docs-reorg.md) has landed (branch `docs-reorg`):

| Change | Status |
|--------|--------|
| C1 — drop guide integer prefixes; order via toctree | Done |
| C2 — re-layer guide toctree into Core + Internals (optional) | Done |
| C3 — move references-and-storage-indirection deep-dive into `reference/concepts/`, leave a guide glance | Done |
| C4 — wire guide glance → concepts diverts | Done |
| C5 — add `reference/concepts/choosing-an-async-construct.md` decision aid | Done |
| C6 — add `patterns/recipes/embedding-host.md`; repoint from-python/where-next | Done |
| C7 — reconcile this tracker + sanction the concepts tier in documentation-goals.md | Done |

`reference/concepts/` is now a **sanctioned IA tier** (guide = glance, concepts
= authoritative deep-dive; see documentation-goals.md § 2.3 and § 4.4.2). The
per-page status cells below still predate the tracks being written and are
**pending a full re-audit** — treat "Stub" as stale for start/ and guide/ pages
that now carry real content.

---

## Legend

| Status | Meaning |
|--------|---------|
| **Done** | Real content, expected to build in Sphinx |
| **Generated** | Adapted from `design/` reference via split tool |
| **Partial** | Some content; incomplete vs goals |
| **Stub** | Placeholder with Under construction admonition |
| **Not started** | No page or tooling yet |

---

## documentation-goals.md section 4 (information architecture)

### 4.1 start/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| overview | Done | `docs/start/overview.md` | |
| install-run | Done | `docs/start/install-run.md` | |

### 4.2 start/beginner/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| 00-hello-change | Stub | `docs/start/beginner/00-hello-change.md` | |
| 01-words-and-punctuation | Stub | `docs/start/beginner/01-words-and-punctuation.md` | |
| 02-calling-things | Stub | `docs/start/beginner/02-calling-things.md` | |
| 03-results-and-messages | Stub | `docs/start/beginner/03-results-and-messages.md` | |
| 04-repeating-patterns | Stub | `docs/start/beginner/04-repeating-patterns.md` | |
| where-next | Stub | `docs/start/beginner/where-next.md` | |

### 4.3 start/from-python/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| map | Stub | `docs/start/from-python/map.md` | |
| runtime-and-tooling | Stub | `docs/start/from-python/runtime-and-tooling.md` | |
| data-and-control | Stub | `docs/start/from-python/data-and-control.md` | |
| functions-and-scope | Stub | `docs/start/from-python/functions-and-scope.md` | |
| errors-and-debugging | Stub | `docs/start/from-python/errors-and-debugging.md` | |
| interop | Stub | `docs/start/from-python/interop.md` | |
| where-next | Stub | `docs/start/from-python/where-next.md` | |

### 4.4 guide/

Unnumbered; ordered by `guide/index.md` toctree (Core, then Internals). Each
Core chapter is a glance that diverts to a `reference/concepts/` page (§ 4.4.2).

| Page | Group | Status | Doc path | Notes |
|------|-------|--------|----------|-------|
| model-of-evaluation | Core | Done | `docs/guide/model-of-evaluation.md` | diverts → layout-and-s-expressions, demand-async-and-defer |
| forms-and-combining | Core | Done | `docs/guide/forms-and-combining.md` | diverts → layout-and-s-expressions, brackets-and-call-convention |
| bindings-and-environments | Core | Done | `docs/guide/bindings-and-environments.md` | diverts → bindings-and-refs, references-and-storage-indirection |
| references-and-storage-indirection | Core | Done | `docs/guide/references-and-storage-indirection.md` | glance; deep-dive moved to concepts (C3) |
| functions-deep | Core | Done | `docs/guide/functions-deep.md` | diverts → demand-async-and-defer, choosing-an-async-construct |
| data-structures | Core | Done | `docs/guide/data-structures.md` | no concepts counterpart (intentional) |
| control-flow | Core | Done | `docs/guide/control-flow.md` | diverts → demand-async-and-defer |
| modules-and-program-structure | Core | Done | `docs/guide/modules-and-program-structure.md` | no concepts counterpart (intentional) |
| macros-or-syntax-extension | Core | Done | `docs/guide/macros-or-syntax-extension.md` | diverts → macro-matching-and-hygiene |
| types-or-contracts | Core | Done | `docs/guide/types-or-contracts.md` | thin; keep/merge decided per-chapter later |
| io-and-effects | Core | Done | `docs/guide/io-and-effects.md` | thin; keep/merge decided per-chapter later |
| testing-and-quality | Core | Done | `docs/guide/testing-and-quality.md` | diverts → project/build-test; overlap flagged |
| bytecode-mental-model | Internals | Done | `docs/guide/bytecode-mental-model.md` | optional |
| from-source-to-stack | Internals | Done | `docs/guide/from-source-to-stack.md` | optional |

### 4.4.2 reference/concepts/ (authoritative deep-dives)

Sanctioned tier (documentation-goals.md § 4.4.2); the deep-dive backing for the
guide glance chapters.

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| bindings-and-refs | Done | `docs/reference/concepts/bindings-and-refs.md` | |
| references-and-storage-indirection | Done | `docs/reference/concepts/references-and-storage-indirection.md` | moved from guide (C3) |
| brackets-and-call-convention | Done | `docs/reference/concepts/brackets-and-call-convention.md` | |
| demand-async-and-defer | Done | `docs/reference/concepts/demand-async-and-defer.md` | |
| choosing-an-async-construct | Done | `docs/reference/concepts/choosing-an-async-construct.md` | decision aid (C5) |
| layout-and-s-expressions | Done | `docs/reference/concepts/layout-and-s-expressions.md` | |
| macro-matching-and-hygiene | Done | `docs/reference/concepts/macro-matching-and-hygiene.md` | |
| pragma | Done | `docs/reference/concepts/pragma.md` | |

### 4.5 reference/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| grammar | Stub | `docs/reference/grammar.md` | Needs SYNTAX.md adaptation |
| reader-layout | Done | `docs/reference/reader-layout.md` | Normative layout syntax + EBNF |
| layout-and-s-expressions | Done | `docs/reference/concepts/layout-and-s-expressions.md` | Three surfaces, YAML/JSON model |
| layout-handlers | Generated | `docs/reference/layouts/` | 17 handler pages from LAYOUTS.md |
| special-forms | Generated | `docs/reference/specials/` | 89 pages from SPECIALS.md |
| stdlib | Not started | `docs/reference/stdlib/` | Use macros + builtins API for now |
| cli-and-config | Done | `docs/reference/cli-and-config.md` | |
| glossary | Partial | `docs/reference/glossary.md` | Starter terms only |
| error-codes | Stub | `docs/reference/error-codes.md` | |

### 4.6 reference/vm/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| overview | Generated | `docs/reference/vm/index.md` | From OPCODES.md ss1 |
| bytecode-format | Partial | `docs/reference/vm/index.md` | Folded into overview; may split later |
| opcode-index | Generated | `docs/reference/vm/opcodes/index.md` | Glob toctree |
| opcodes | Generated | `docs/reference/vm/opcodes/kos-op-*.md` | 217 pages |
| opcode-families | Generated | `docs/reference/vm/families.md` | From OPCODES.md ss2 |
| disassembly-and-debugging | Partial | `docs/reference/vm/index.md` + `api/koskari-dis.md` | |
| peephole-and-lowering-notes | Not started | | In design/OPCODES.md ss1.8 |

### 4.7 patterns/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| index | Done | `docs/patterns/index.md` | |
| recipes/* | Done | `docs/patterns/recipes/` | 8 recipes: hello, countdown, script-main, packaging-installed-scripts, embedding-host (C6), point-class, property-accessors, fibonacci |

### 4.8 project/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| build-test | Done | `docs/project/build-test.md` | |
| architecture-map | Done | `docs/project/architecture-map.md` | |
| vm-and-opcodes | Done | `docs/project/vm-and-opcodes.md` | |
| release-and-versioning | Stub | `docs/project/release-and-versioning.md` | |

### 4.9 corpus/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| SCHEMA.md | Done | `docs/corpus/SCHEMA.md` | |
| SCHEMA.json | Done | `docs/corpus/SCHEMA.json` | Draft structural schema |
| README.md | Done | `docs/corpus/README.md` | |
| datasets.md | Partial | `docs/corpus/datasets.md` | Philosophy only; no HF publish |
| records/ | Partial | `docs/corpus/records/` YAML shards (conditionals, variables, objects, functions, syntax, specials) | 33 + 30 + 41 + 21 + 32 + 14 seeds |

### 4.10 Cross-cutting

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| changelog | Stub | `docs/changelog.md` | |
| faq | Done | `docs/faq.md` | |

### API autodoc

| Area | Status | Doc path | Notes |
|------|--------|----------|-------|
| Python API | Done | `docs/api/` | 22 modules; excludes koskari.bootstrap |
| koskari.builtins | Done | `docs/api/koskari-builtins.md` | Virtual module; `.. kos-builtins::` (Runtime/Compile/Reader) |
| koskari.extras | Done | `koskari/extras/` | Namespace; pygments, sphinx, lsp optional extras |
| koskari.bootstrap | N/A | | Intentionally omitted |

### Macros reference (from MACROS.md)

| Area | Status | Doc path | Notes |
|------|--------|----------|-------|
| Bootstrap macros | Generated | `docs/reference/macros/` | 44 pages |

---

## documentation-goals.md section 5 (MVP v1 checklist)

| MVP item | Status |
|----------|--------|
| Both start/ tracks through where-next.md | **Partial** -- stubs only |
| guide/ through modules + testing | **Stub** |
| reference/glossary.md | **Partial** |
| reference/special-forms.md | **Generated** (per-form pages) |
| reference/cli-and-config.md | **Done** |
| reference/vm/overview + opcode detail | **Generated** |
| patterns/ 3-5 recipes | **Done** (8 recipes) |

---

## corpus-schema-philosophy.md deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Corpus-first schema docs | **Done** | SCHEMA.md + SCHEMA.json |
| Corpus records | **Partial** | `records/conditionals.yaml` (33), `variables.yaml` (30), `objects.yaml` (41), `functions.yaml` (21), `syntax.yaml` (32), `specials.yaml` (14) |
| Corpus test runner | **Not started** | tests/ implicit today |
| Docs embed from corpus | **Not started** | |
| HF dataset views | **Not started** | datasets.md describes pipeline |
| Provenance on training rows | **Not started** | |

---

## Reference inventory (generated counts)

Last regen: `tox -e regen-docs-ref`

| Source | Pages | opcodes.incl |
|--------|-------|--------------|
| Opcodes | 217 | 215 OP() entries (+ index pages) |
| Specials | 89 | includes object-operator stubs |
| Layouts | 17 | |
| Macros | 42 | |

**Drift CI:** not implemented. Manual check: new opcode in `opcodes.incl`
needs matching `docs/reference/vm/opcodes/kos-op-*.md`.

---

## Maintenance backlog

1. Write beginner and from-python tutorial tracks
2. Write guide spine (evaluation model first)
3. Adapt SYNTAX.md into reference/grammar.md
4. Expand glossary; link from tutorials
5. Split peephole/lowering into dedicated VM page if needed
6. Enrich object-operator special pages (currently point to design tables)
7. Corpus records + verification runner + test extraction
8. HF dataset transform tooling
9. Opcode/page count drift checker in CI
10. reference/stdlib/ pages from builtins help registry

---

## Tooling

| Tool | tox env | Purpose |
|------|---------|---------|
| split_reference.py | regen-docs-ref | design/ -> docs/reference/ |
| bootstrap_handwritten.py | regen-docs-ref | start, guide stubs, corpus, etc. |
| bootstrap_api.py | regen-docs-ref | docs/api/ automodule stubs |
| Sphinx | sphinx | build site to build/docs/ |

---

## See also

- [documentation-goals.md](../documentation-goals.md)
- [corpus-schema-philosophy.md](../corpus-schema-philosophy.md)
