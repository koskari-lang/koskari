---
license: gpl-3.0
configs:
  - config_name: corpus
    default: true
    data_files:
      - split: train
        path: koskari-corpus/train.parquet
  - config_name: docs
    data_files:
      - split: train
        path: koskari-docs/train.parquet
  - config_name: programs
    data_files:
      - split: train
        path: koskari-programs/train.parquet
---

# Koskari language corpus

Verified Koskari language examples exported from the Koskari conformance
corpus. Each row traces to a corpus record with machine-checked expectations
at one or more language phases (read, expand, eval, error, invalid).

Source repository: [github.com/obriencj/koskari](https://github.com/obriencj/koskari)

## Configurations

### corpus (default)

Every corpus record. One row per record regardless of which verify stages
are present. Missing stages are null.

| Column | Description |
|--------|-------------|
| `corpus_id` | Unique corpus record identifier |
| `feature` | Feature path (e.g. `special/compile/cond`, `layout/if`) |
| `source` | Koskari source text |
| `read` | Expected reader output from `verify.read` |
| `expand` | Expected macro expansion from `verify.expand` |
| `eval` | Expected eval result from `verify.eval` |
| `error` | Expected pipeline error from `verify.error` |
| `invalid` | Prose explanation from `verify.invalid` |
| `references` | Links to user documentation pages under `docs/` |
| `stability` | Record stability label (`stable`, `evolving`, `experimental`) |
| `tags` | Maintainer tags (not copied from training-only metadata) |
| `schema_version` | Corpus schema identifier at export time |
| `git_ref` | Short git commit at export time |

### docs

User documentation pages (Markdown) from the source repository's
`docs/reference/`, `docs/start/`, `docs/guide/`, `docs/patterns/`, and
`docs/agents/` trees. One row per page. The `path` column matches the
strings in the `corpus` config's `references` column verbatim, so a
record's references resolve directly to their documentation content.

The `agents` section is written for AI consumers rather than humans:
dense normative rules, valid/invalid pairs, and guidance on querying
this dataset. Filter with `section == "agents"`.

| Column | Description |
|--------|-------------|
| `path` | Docs-relative page path (e.g. `reference/specials/compile-define.md`) |
| `title` | Page title (first Markdown heading) |
| `summary` | First paragraph, plain text |
| `content` | Full raw Markdown of the page |
| `section` | Parent directory (e.g. `reference/specials`) |
| `kind` | Always `doc` |
| `schema_version` | Corpus schema identifier at export time |
| `git_ref` | Short git commit at export time |

**Whole-file cells:** each page is one `content` cell. The Hugging Face
Dataset Viewer caps cell size in API responses, so a very large page would
be truncated. Pages are small today (a few KiB); chunk-by-heading is a
deferred mitigation should pages grow. See `tools/corpus/views/docs.ki`.

### programs

Example Koskari programs (whole `.ki` files and their READMEs) from the
source repository's `examples/` tree, excluding build artifacts. These give
composition-level context — full programs rather than single-construct
snippets.

**These rows are illustrative and NOT verified.** Unlike the `corpus`
config, they carry no machine-checked read/expand/eval expectations; they
are working example source, not conformance assertions.

| Column | Description |
|--------|-------------|
| `path` | Repo-relative file path (e.g. `examples/life/koslife/life.ki`) |
| `title` | File stem, or README heading |
| `summary` | Module docstring or leading comment, plain text |
| `content` | Full raw source of the file |
| `section` | Top-level example group (e.g. `examples/life`) |
| `kind` | Always `example` |
| `schema_version` | Corpus schema identifier at export time |
| `git_ref` | Short git commit at export time |

## Verify stages

Each corpus record asserts behaviour at language boundaries:

- **read** — reader and layout output
- **expand** — macro expansion on the outer form
- **eval** — runtime result (including `@`-marked complex encodings in source)
- **error** — expected pipeline failure
- **invalid** — documented illegal source (may coexist with read expectations)

Only stages present in the source corpus record appear as non-null columns in
the `corpus` config.

## Provenance

Every row includes `corpus_id`, `schema_version`, and `git_ref` so downstream
consumers can join back to the authoritative YAML record and the export
revision.

## License

GNU General Public License v3. See the Koskari repository for full terms.
