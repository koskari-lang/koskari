# Documentation tracker

Living status for the user documentation site under `docs/`, correlated with
[documentation-goals.md](documentation-goals.md) and
[corpus-schema-philosophy.md](corpus-schema-philosophy.md).

Build: `make docs` or `tox -e sphinx` (output: `build/docs/`).

Regenerate reference splits: `tox -e regen-docs-ref` (overwrites generated
reference pages and bootstrap scripts output).

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

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| 00-model-of-evaluation | Stub | `docs/guide/00-model-of-evaluation.md` | |
| 01-forms-and-combining | Stub | `docs/guide/01-forms-and-combining.md` | |
| 02-bindings-and-environments | Stub | `docs/guide/02-bindings-and-environments.md` | |
| 03-functions-deep | Stub | `docs/guide/03-functions-deep.md` | |
| 04-data-structures | Stub | `docs/guide/04-data-structures.md` | |
| 05-control-flow | Stub | `docs/guide/05-control-flow.md` | |
| 06-modules-and-program-structure | Stub | `docs/guide/06-modules-and-program-structure.md` | |
| 07-macros-or-syntax-extension | Stub | `docs/guide/07-macros-or-syntax-extension.md` | |
| 08-types-or-contracts | Stub | `docs/guide/08-types-or-contracts.md` | |
| 09-io-and-effects | Stub | `docs/guide/09-io-and-effects.md` | |
| 10-testing-and-quality | Stub | `docs/guide/10-testing-and-quality.md` | Partial overlap with project/build-test |
| bytecode-mental-model | Stub | `docs/guide/bytecode-mental-model.md` | |
| from-source-to-stack | Stub | `docs/guide/from-source-to-stack.md` | |

### 4.5 reference/

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| grammar | Stub | `docs/reference/grammar.md` | Needs SYNTAX.md adaptation |
| reader-layout | Generated | `docs/reference/layouts/` | 17 layout pages from LAYOUTS.md |
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
| recipes/* | Partial | `docs/patterns/recipes/hello.md` | One recipe; target 3-5 for v1 |

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
| records/ | Not started | `docs/corpus/records/` | Empty placeholder |

### 4.10 Cross-cutting

| Page | Status | Doc path | Notes |
|------|--------|----------|-------|
| changelog | Stub | `docs/changelog.md` | |
| faq | Done | `docs/faq.md` | |

### API autodoc

| Area | Status | Doc path | Notes |
|------|--------|----------|-------|
| Python API | Done | `docs/api/` | 22 modules; excludes koskari.bootstrap |
| koskari.builtins | Done | `docs/api/koskari-builtins.md` | Virtual module documented |
| koskari.bootstrap | N/A | | Intentionally omitted |

### Macros reference (from MACROS.md)

| Area | Status | Doc path | Notes |
|------|--------|----------|-------|
| Bootstrap macros | Generated | `docs/reference/macros/` | 42 pages |

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
| patterns/ 3-5 recipes | **Partial** (1 recipe) |

---

## corpus-schema-philosophy.md deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Corpus-first schema docs | **Done** | SCHEMA.md + SCHEMA.json |
| Corpus records | **Not started** | records/ empty |
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

- [documentation-goals.md](documentation-goals.md)
- [corpus-schema-philosophy.md](corpus-schema-philosophy.md)
