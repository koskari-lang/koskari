# Corpus generation task

You are generating conformance corpus entries for the Koskari language — a Lisp
dialect implemented in Python over a custom C VM. Entries land in
`docs/corpus/records/` and downstream feed a HuggingFace dataset that trains
agents to write idiomatic Koskari. Design rationale:
[design/corpus-generation-agent.md](../../design/corpus-generation-agent.md).

Your job is volume *and* correctness: many distinct entries, each with `verify`
fields that come from the real implementation — never from guessing.

## Environment (read first)

You are running in a dedicated git worktree, not the main development tree.
Commit your work freely as you go; a human opens the PR.

**Tox-rule override.** The root `CLAUDE.md` says all Python execution must go
through tox. For this corpus task that rule does not apply. Instead:

1. Run `make venv` once at the start of the session. This builds the C
   extension into this worktree's `.venv` (`uv pip install -qe .[repl]`).
2. Install the corpus tooling deps once (not in the `repl` extra):
   `uv pip install jsonschema PyYAML`.
3. Drive the implementation only through `tools/corpus/quick.py`, which uses
   `uv run` against that `.venv`. Do not invoke `tox`, `pytest`, `python`, or
   `python3` directly for corpus work — `quick.py` is the single sanctioned
   entry point.

You never edit C or `.ki` source here, so the build stays stable for the whole
session. The PR's `make test` is the authoritative final check.

**Smoke test before generating.** Confirm the toolchain end to end:

```
uv run python tools/corpus/quick.py verify --all '(cond [True 100])'
# -> {"read": "(cond (True 100))", "expand": "(cond (True 100))", "eval": "100"}
```

## Your tools

All commands run as `uv run python tools/corpus/quick.py <subcommand>`.

### `verify` — the oracle (advisory)

Runs a source form through the real implementation and emits corpus-encoded
strings. ALWAYS use this — never invent verify output.

```
quick.py verify [--read] [--expand] [--eval] [--all]
                [--block] [--feature <f>] [--fixture <json>]
                ['<form>']
```

- The form is the positional argument, or piped on stdin when omitted.
- With no stage flag (or `--all`) it emits all three stages. Pick individual
  stages with `--read` / `--expand` / `--eval`.
- `--block` reads with the layout/indentation reader (for block-surface forms).
- `--feature <f>` sets the feature string (also influences reader selection).
- `--fixture '<json>'` merges extra eval bindings, e.g.
  `--fixture '{"n": 42}'`. Baked-in fixtures already available to `--eval`:
  `seq=(1,2,3)`, `obj` (`.count=5`, `.value=99`), `add`, `deref`, `f`, `z=1`,
  `a=0`.

Output is a JSON object. Example:

```
$ quick.py verify --all '(incr! z)'
{
  "read": "(incr! z)",
  "expand": "(compile/inplace + z 1)",
  "eval": "null"
}
```

Eval encoding: `verify` only emits an `eval` string it has **verified** matches
the runtime value (it round-trips through the same comparator `insert` uses), so
you can paste it directly. When it cannot auto-encode the result it instead
returns `eval_raw` (the Python repr), `eval_type`, and a `warnings` entry — then
you must hand-write a literal or `@marked:` encoding (see
[docs/corpus/SCHEMA.md](SCHEMA.md)) and confirm it with `insert --dry-run`. A
stage that errors appears under `errors` (or, for a single requested stage, as
`{"error": ..., "stage": ...}` with a non-zero exit).

### `insert` — validate and write (authoritative)

Reads one candidate YAML record (stdin by default, or `--file`), then:
schema-checks it, rejects a duplicate `id`, and runs **every** verify stage in
the record through the real handlers (`read`/`expand`/`eval`/`error`). On
success it appends the record to a shard, leaving existing records untouched.

```
quick.py insert [--file <path>] [--shard <name>] [--dry-run]
```

- `--dry-run` validates without writing — use it while iterating.
- `--shard <name>` picks the target shard (recommended). When omitted, the
  shard is inferred from the record's category tag, routing records that carry a
  `verify.invalid` or `verify.error` stage to the `*-invalid` shard. If inference
  fails it tells you to pass `--shard`.
- Output is `OK <id> (...)` on success or `FAIL <id> <reason>` (non-zero exit)
  on rejection. Always check it; on `FAIL`, fix the entry and retry.

```
$ quick.py insert --shard conditionals < entry.yaml
OK conditional-cond-else-only-002 (inserted into docs/corpus/records/conditionals.yaml)
```

### `coverage` — find gaps (start here each session)

```
quick.py coverage [--min-entries N] [--feature <f>]
```

- `--min-entries N` (default 3) lists features with fewer than N entries,
  sorted ascending (`count<TAB>feature`).
- `--feature <f>` prints the count and entry ids for one feature.
- Note: it reports only features that already appear in the corpus; gap
  analysis against the full special/macro registry is not yet implemented, so
  also consult [design/SPECIALS.md](../../design/SPECIALS.md) and
  [design/MACROS.md](../../design/MACROS.md) for entirely-missing features.

### Typical loop

1. `quick.py coverage` → pick a sparse feature.
2. `quick.py verify --all '<form>'` → get the verify strings.
3. Write the YAML record (format below), pasting the verified strings.
4. `quick.py insert --dry-run --shard <name> < entry.yaml` → confirm it passes.
5. `quick.py insert --shard <name> < entry.yaml` → write it. Commit.

## Rules

1. Never write a `verify` field without running `quick.py verify` first.
2. Never duplicate an existing `id`. Check `quick.py coverage` first.
3. Prefer realistic source forms extracted from `examples/` and
   `koskari/bootstrap/` over toy examples. Toy examples are acceptable for edge
   cases only.
4. If eval produces an unexpected exception, do not suppress it — write an
   `invalid` entry with `notes: corrective: <the fixed form>`.
5. If `quick.py insert` fails, fix the entry and retry. Do not skip.
6. Tag `surface:block` on any entry that tests layout/indentation syntax.
7. When unsure what a form should expand or evaluate to, check
   `docs/corpus/records/` for similar existing entries first.
8. **Write `source:` as a block scalar** (`source: |`) so colons, brackets,
   quotes, and leading special characters in Koskari code never collide with
   YAML syntax — e.g. `(cond [else: 5])` is invalid unquoted YAML (the `: `
   reads as a mapping). Keep `verify` strings double-quoted as shown. If
   `insert` reports a `ScannerError`/`FAIL <unparsed>`, your YAML is malformed,
   not your entry.

## What to produce

For each builtin special ([design/SPECIALS.md](../../design/SPECIALS.md)) and
macro ([design/MACROS.md](../../design/MACROS.md)), aim for at least:

1. **Basic** — simplest correct invocation, eval verified.
2. **Variant** — different argument count, nested form, or edge case.
3. **Block/layout** — where a layout handler exists (verify the read stage; tag
   `surface:block`).
4. **Invalid** — an intentionally wrong form. Keep `stability: evolving`, mark
   the failure with `verify.invalid: <why it's wrong>` (runtime/eval failure or
   semantic gotcha) or `verify.error: <ExceptionType: message>` (read/compile-time
   failure), add the `invalid` tag, and give `notes: corrective: <fix>`. There is
   no `invalid` stability value — the schema rejects it.

Priority order (highest training value first):

1. Patterns common in real code: `define-class`, `for-each` + destructuring,
   `demand`, `define-syntax` with `macro-rules`, `try/except`, f-strings,
   `let`, async patterns.
2. All specials with < 3 entries.
3. All macros with < 3 entries.
4. Layout/block variants of anything that already has an s-expression entry.

## Entry format

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
  # For a wrong form, use ONE of these instead of eval:
  #   error:   "<ExceptionType: message>"  — read/compile-time failure (asserted)
  #   invalid: "<prose: why it's wrong>"   — runtime (eval) failure or semantic
  #                                          gotcha; not run, so also keep a
  #                                          verify.read to pin the parse
references:
  - reference/specials/compile-<name>.md   # flatten the feature path with a
                                           # dash, e.g. special/compile/cond ->
                                           # reference/specials/compile-cond.md
stability: evolving                # one of stable | evolving | experimental
                                   # (there is NO "invalid" stability — mark a
                                   # wrong form with verify.invalid / verify.error
                                   # and the `invalid` tag instead). stable only
                                   # for forms whose output is extremely unlikely
                                   # to change.
tags:
  - <category>
  - <feature-slug>
  - surface:block                  # if testing layout syntax
  - invalid                        # if a wrong form (with surprising for a
                                   # form that reads/evals but misleads)
  - <intent>                       # goal word(s) from the closed intent
                                   # vocabulary in SCHEMA.md, e.g. cleanup,
                                   # catch, loop, mutate, branch, define-type
notes: <optional — source file, fixture deps, corrective fix>
```

## Volume target

Aim for 20–30 distinct entries per session, each testing something different,
before pausing for review. Quality over quantity.

## Reference docs

- [design/SPECIALS.md](../../design/SPECIALS.md) — compiler special forms
- [design/MACROS.md](../../design/MACROS.md) — builtin macros by bootstrap file
- [design/SYNTAX.md](../../design/SYNTAX.md) — reader/layout grammar
- `docs/corpus/records/` — existing entries, for style and de-duplication

# The end.
