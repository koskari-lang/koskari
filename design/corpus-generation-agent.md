# Corpus generation agent

A plan for growing `docs/corpus/records/` to hundreds–thousands of entries
covering every builtin special and macro, suitable for a downstream HuggingFace
dataset that trains agents to write idiomatic Koskari code. This augments the
schema and publication model in
[corpus-schema-philosophy.md](corpus-schema-philosophy.md) with a concrete
generation workflow.

The intent is that corpus growth happens autonomously via a Claude Code agent,
not at the cost of core Koskari development time. The agent runs in its own git
worktree so it can commit freely and run for hours in the background while
active development continues on the main tree; a human opens a PR from the
worktree branch once a batch is done and verified.

---

## Constraints

- Every `verify` field must come from the actual implementation — no guessing.
- The corpus schema and validation framework already exist
  (`docs/corpus/SCHEMA.json`, `tools/corpus/`); new entries must pass them.
- Duplicate coverage (same feature, same form) is wasteful; the agent must
  query existing coverage before generating.
- The agent should run unsupervised for hours; it needs tight feedback loops
  (oracle + validator) so it can self-correct without human input.

---

## Isolation and environment

The agent operates in a dedicated git worktree, not the live development tree.
This buys two things specific to a long background run:

- **A drift-free oracle.** The worktree is a snapshot of the
  implementation. The agent only ever writes YAML records (never C or `.ki`
  source), so the built extension it validates against stays stable for the
  whole session — entries are verified against one coherent implementation
  state, then merged. The live tree can be rebuilt and half-edited freely
  without perturbing the agent.
- **No contention.** A separate worktree (with its own `.venv`, which is
  gitignored) means the agent's many oracle invocations never collide with the
  developer's own `tox` / `make test` runs in the main tree.

Setup is a `make venv` in the worktree: it runs `uv pip install -qe .[repl]`,
an editable install that builds the C extension into the worktree's `.venv`.
The corpus tooling additionally needs `jsonschema` and `PyYAML`, which live in
the tox testenv deps rather than the `repl` extra, so install them too:
`uv pip install jsonschema PyYAML`. After that, `uv run python` / `uv run
koskari` give a fully functional Koskari to validate against. The build sentinel
depends on the native sources, so it only rebuilds when C changes — which never
happens during a corpus run. (A fresh worktree has no stale sentinel, so the
first `make venv` always compiles.)

**Tox-rule scope.** The root CLAUDE.md requires all Python execution to go
through tox. Inside this isolated worktree that rule is relaxed for the corpus
task only: the tooling reaches the implementation via `uv run` against the
worktree's own `.venv`, not tox. This is not a general license to invoke
`python` ad hoc — the bypass lives inside the corpus tooling (see `quick`
below) so there is exactly one sanctioned non-tox entry point. The canonical
`make test` on the eventual PR remains the authoritative backstop.

---

## Tooling to build first

The minimum viable tooling is a single multi-command script,
`tools/corpus/quick.ki` (implemented), alongside the existing corpus tooling
(`export.ki`, `load.ki`). One file rather than three because importing Koskari
compiles the bootstrap `.ki` files on every launch — a shared script lets
verify / insert / coverage reuse one warmup and one set of helpers, and gives
the agent exactly one entry point to discover. It reaches the implementation via
`uv run` against the worktree's `.venv` (the single sanctioned non-tox path; see
*Isolation and environment* above).

Crucially, `quick` is a thin wrapper over the **existing record runner** in
`tests/corpus/__init__.py` (`CorpusTestCase`, the `HANDLERS` for
read/expand/eval/error, `load_corpus_index`, the eval encode/compare helpers)
and the YAML serializer in `tools/corpus/load.ki` (`dump_record`). It does not
reimplement read/expand/eval, verification, or serialization — so its output is
exactly what the corpus test phase (`phase_corpus`) accepts. The agent-facing
CLI contract lives in `docs/corpus/CLAUDE.md`; the sketch below is the rationale.

### `quick verify` — the oracle

Accepts a source form and one or more stage flags, runs it through the
implementation, and returns the verify strings.

```
Usage:
  quick verify --read   '<form>'
  quick verify --expand '<form>'
  quick verify --eval   '<form>'
  quick verify --all    '<form>'

Output: JSON  { "read": "...", "expand": "...", "eval": "..." }
Errors: non-zero exit + JSON { "error": "...", "stage": "read|expand|eval" }
```

Notes:

- The form is positional, or read from stdin when omitted; `--block`,
  `--feature`, and `--fixture '<json>'` (extra eval bindings) are supported. A
  set of fixtures (`seq`, `obj`, `add`, `f`, `z`, `a`, `deref`) is baked in via
  the runner.
- The emitted `eval` string is **verified**: `quick` only outputs an encoding
  that round-trips through the same parse + compare helpers `insert` uses, so it
  is safe to paste. When it cannot auto-encode (e.g. a proper list of symbols, a
  a class, a callable) it returns `eval_raw` + `eval_type` + a warning instead,
  and the author hand-writes a literal or `@marked:` encoding and confirms it
  with `insert --dry-run`.

### `quick insert` — insert and validate

Accepts a candidate YAML record (from stdin or file), inserts it into the
correct records file, runs the corpus validation framework, and reports
pass/fail with detail.

```
Usage:
  quick insert < candidate.yaml                 # infer shard, write
  quick insert --shard <name> < candidate.yaml  # explicit shard (preferred)
  quick insert --dry-run < candidate.yaml        # validate only, don't write

Output:
  OK   <id>  (inserted into docs/corpus/records/<category>.yaml)
  FAIL <id>  <reason>
```

Notes:

- Schema-validates, rejects duplicate `id`s, and runs **every** verify stage in
  the record (`read`/`expand`/`eval`/`error`) through the real handlers — fast
  per-entry feedback without running the whole corpus.
- Target shard comes from `--shard` (preferred) or is inferred from the
  category tag, routing `stability: invalid` / `error` entries to the
  `*-invalid` shard. Inference failure asks for `--shard`.
- Appends the new record as a fresh YAML document, leaving existing records
  byte-for-byte unchanged (minimal diff), rather than rewriting the shard.

### `quick coverage` — coverage query

Reports which features have fewer than N entries, so the agent knows where to
focus.

```
Usage:
  quick coverage --min-entries 3
  quick coverage --feature 'special/compile/cond'

Output: table of count -> feature, sorted ascending
```

Reports only features already present in the corpus; gap analysis against the
full special/macro registry is not yet implemented (consult `SPECIALS.md` /
`MACROS.md` for entirely-missing features). Enumerating the registry
programmatically is a natural follow-up.

---

## Entry format reference

```yaml
---
id: <category>-<feature-slug>-<variant>-<NNN>
feature: special/compile/<name>   # or macro/compile/<name> or layout/<name>
source: |
  <koskari source form — single expression or begin block>
verify:
  read:   "<reader output — omit if not testing read stage>"
  expand: "<macro-expanded form — omit if not testing expand stage>"
  eval:   "<eval result string>"
references:
  - reference/specials/<name>.md      # or reference/macros/
  - reference/layouts/<name>.md
stability: evolving                   # or stable
tags:
  - <category>                        # conditional, function, class, variable, etc.
  - <feature-slug>
  - surface:block                     # if testing layout/block syntax
notes: <optional — source file, fixture deps, gotchas>
```

ID naming convention: `<category>-<feature>-<variant>-<NNN>`. Examples:
`conditional-cond-else-only-001`, `class-layout-dataclass-decorator-001`.

---

## Agent task mandate

For every builtin special ([SPECIALS.md](SPECIALS.md)) and every builtin macro
([MACROS.md](MACROS.md)), produce at minimum:

1. **Basic usage** — simplest correct invocation, eval verified.
2. **Variant** — different argument count, nested form, or edge case.
3. **Block/layout form** — where a layout handler exists (verify read stage).
4. **Error/invalid form** — where the form is intentionally wrong, with a
   corrective note (mark `stability: invalid` and include
   `notes: corrective: <fix>`).

Additional entries from:

- Bootstrap `.ki` source files (extract subforms mechanically).
- Example files (`examples/life.ki`, `fib.ki`, `curse.ki`, `fastapi/`,
  `scheduler/`).
- Existing test coverage in `tests/` (backfill corpus entries for tested
  behaviors).

Priority order (highest value for HuggingFace training):

1. Patterns that appear repeatedly in real code: `define-class`, `for-each` +
   destructuring, `demand`, `define-syntax` with `macro-rules`, `try/except`,
   f-strings, `let`, async patterns.
2. All specials with < 3 entries.
3. All macros with < 3 entries.
4. Layout/block variants of anything that has an s-expression entry.

---

## Agent session CLAUDE.md

Place a generation-task `CLAUDE.md` in `docs/corpus/` so an agent session rooted
there picks it up. It should cover:

- **Setup:** run `make venv` once in the worktree, then drive the implementation
  through `uv run` via `quick` — do not use tox for the corpus task.
- **Tools:** `quick verify --all '<form>'` (always use it — never invent
  verify output); `quick insert < entry.yaml` (check exit code; FAIL means
  rejected and why); `quick coverage --min-entries 3` (start here each
  session).
- **Rules:** never write a verify field without running the oracle first; never
  duplicate an existing id (check coverage first); prefer realistic forms from
  `examples/` and `bootstrap/` over toy examples (toys are for edge cases only);
  if eval produces an unexpected exception, write an `invalid` entry with
  `notes: corrective: <fixed form>` rather than suppressing it; if insert fails,
  fix and retry — do not skip; tag `surface:block` on any layout/indentation
  entry; when in doubt about expand/eval output, check existing records for
  similar entries.
- **Volume target:** 20–30 distinct entries per session before stopping for
  review. Quality over quantity.
- **Reference docs:** [SPECIALS.md](SPECIALS.md), [MACROS.md](MACROS.md),
  [SYNTAX.md](SYNTAX.md), `docs/corpus/records/` for style.

---

## Phasing

- **Phase 1 — Tooling** (done). `quick` (`verify` / `insert` / `coverage`)
  and `docs/corpus/CLAUDE.md` exist and validate a known-good entry end to end.
  Remaining: set up the worktree and run the smoke test there.
- **Phase 2 — First agent run** (unsupervised, in the worktree). Agent works
  through priority features and commits batches; review the diff / open a PR
  afterward.
- **Phase 3 — Iterate.** Adjust the session `CLAUDE.md` based on what the agent
  got wrong; add the `coverage` subcommand once volume justifies it; re-run
  targeting remaining gaps.
- **Phase 4 — Dataset conversion** (later). HuggingFace format needs a
  conversion step from YAML corpus records; `tools/corpus/export.ki` is the
  natural home. Volume and correctness come first.

---

## Notes

- The corpus validation framework already exists — `quick insert` just
  invokes it. Don't reinvent the wheel.
- `stability: evolving` is correct for almost everything now. Reserve
  `stability: stable` for forms whose expand/eval output is extremely unlikely
  to change (basic arithmetic, `and`/`or` short-circuit, literal reads).

Ref: [corpus-schema-philosophy.md](corpus-schema-philosophy.md);
[documentation-goals.md](documentation-goals.md); `tools/corpus/`;
`docs/corpus/records/`.

# The end.
