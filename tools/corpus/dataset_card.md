---
license: gpl-3.0
configs:
  - config_name: corpus
    default: true
    data_files:
      - split: train
        path: koskari-corpus/train.parquet
  - config_name: eval
    data_files:
      - split: train
        path: koskari-eval/train.parquet
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

### eval

Subset of records that include a verified `verify.eval` expectation. Flat
source-to-eval rows for completion-style use.

| Column | Description |
|--------|-------------|
| `corpus_id` | Unique corpus record identifier |
| `feature` | Feature path |
| `source` | Koskari source text |
| `eval` | Expected eval result |
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
