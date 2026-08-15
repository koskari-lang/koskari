# Documentation Boundaries

Three trees serve different audiences. Respect the walls between them.

## `docs/` -- user-facing

For end users, tutorial readers, and the published Sphinx site: stable
reference, concepts, guides, glossary, patterns, API autodoc.

Must not contain any pointer to `design/`. No paths, no filenames, no "see
design notes", no FAQ entry telling readers where internal docs live. That
includes the internal rollup basenames, which exist only under `design/`:
`MACROS.md`, `SPECIALS.md`, `LAYOUTS.md`, `SYNTAX.md`, `OPCODES.md`,
`EVALUATOR.md`, `MATCHER.md`, `TODO.md`, and the `interpreter/` and
`language/` subtrees.

Also excluded: in-progress design write-ups, agent scratch notes, "adapted
from design/..." placeholders, and stub pages that only defer elsewhere.

When material is missing from `docs/`, add or expand user-facing prose and
examples, or link to an existing page under `docs/reference/`, `docs/start/`,
and so on. Do not link or copy from `design/` to fill the gap.

User documentation must not refer to the corpus.

## `docs/corpus/` -- the example corpus

Generates agent-oriented datasets of language examples. Corpus `references`
and maintainer `notes` follow the same rule as the rest of `docs/`: cite
`docs/` paths only, never `design/`. The corpus may link into the user
documentation for clarification; the reverse does not happen.

## `design/` -- internal

For the maintainer and agents: ideas, drafts, rollups, snapshots, VM and
compiler contracts, work in progress. May link into `docs/` when a stable
user page exists. Nothing in `docs/` may depend on it.

`design/TODO.md` entries are 3-6 line stubs with a Ref link. The detail lives
in the design doc.

## Direction of travel

Settled material may move from `design/` into `docs/`, replacing rollups or
design-only pages there. The reverse never happens. `docs/` never becomes a
staging area for design, and user docs never start linking backward into
`design/`. Where both trees cover a topic, `docs/` must stand alone for its
readers.

## Before committing changes under `docs/`

1. Grep for `design/`, the rollup basenames above, and `interpreter/` paths
   that are not real files under `docs/`.
2. Replace thin stubs with syntax, behavior, and at least one example.
3. Retarget cross-references to concrete pages under `docs/reference/`,
   concepts, layouts, macros, or the VM index, not to the repository root or
   to `design/`.

# The end.
