# Published datasets

Published ML datasets are **views** generated from corpus records, not the
authoritative schema.

```text
Corpus JSONL
      |
      v
 Transformation
      |
      v
 Parquet (optional)
      |
      v
 Hugging Face Dataset
```

## Status

No dataset has been published to Hugging Face yet. Local export tooling
produces parquet under `build/corpus/datasets/`.

Each exported row carries provenance (`corpus_id`, `schema_version`, `git_ref`).

## koskari-eval

Flat rows for corpus records that include `verify.eval`. Records verified
only at other phases (for example `read` or `compile`) are omitted.

| Column | Description |
|--------|-------------|
| `corpus_id` | Corpus record `id` |
| `feature` | Feature path (e.g. `special/compile/cond`) |
| `source` | Koskari source text |
| `eval` | Expected eval result from `verify.eval` |
| `schema_version` | Corpus schema `$id` from [SCHEMA.json](SCHEMA.json) |
| `git_ref` | Short git commit at export time (empty outside a git checkout) |

Output file: `build/corpus/datasets/koskari-eval/train.parquet`

## Export commands

Local export via tox:

```bash
tox -qe regen-corpus -- --view koskari-eval
```

Makefile shortcuts:

```bash
make corpus-parquet
make corpus-parquet-container
```

The container target builds `tools/corpus/Containerfile` and writes parquet
into `build/corpus/datasets/` on the host.

## CI stub

[`.github/workflows/publish_corpus_dataset.yml`](../../.github/workflows/publish_corpus_dataset.yml)
is a manual (`workflow_dispatch`) workflow that runs the export and uploads a
parquet artifact. The publish job is disabled until Hugging Face credentials
and a dataset repo are configured.
