# Published datasets

Published ML datasets are **views** generated from corpus records, not the
authoritative schema.

```text
Corpus YAML
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

The corpus is published to Hugging Face as
[obriencj/koskari](https://huggingface.co/datasets/obriencj/koskari).
Local export tooling produces the same parquet under
`build/corpus/datasets/`.

Each exported row carries provenance (`corpus_id`, `schema_version`, `git_ref`).

Publication is triggered manually via
[`.github/workflows/publish_corpus_dataset.yml`](../../.github/workflows/publish_corpus_dataset.yml)
(`workflow_dispatch`). The publish job authenticates to Hugging Face via
[Trusted Publishers](https://huggingface.co/docs/hub/trusted-publishers) (OIDC);
no `HF_TOKEN` secret is stored in GitHub.

On the dataset repo settings at
`https://huggingface.co/datasets/obriencj/koskari/settings`, configure a
trusted publisher with:

- Provider: GitHub Actions
- `repository` = `obriencj/koskari`
- `workflow` = `publish_corpus_dataset.yml`

## Configurations

The Hub dataset exposes two configurations. The dataset card lives at
`tools/corpus/dataset_card.md` in the repository and is uploaded as the Hub
`README.md`.

### corpus (default)

Every corpus record. One row per record with columns for each verify stage
that may be present.

| Column | Description |
|--------|-------------|
| `corpus_id` | Corpus record `id` |
| `feature` | Feature path (e.g. `special/compile/cond`) |
| `source` | Koskari source text |
| `read` | Expected reader output from `verify.read` |
| `expand` | Expected macro expansion from `verify.expand` |
| `eval` | Expected eval result from `verify.eval` |
| `error` | Expected pipeline error from `verify.error` |
| `invalid` | Prose explanation from `verify.invalid` |
| `references` | Links to pages under `docs/` |
| `stability` | Record stability label |
| `tags` | Maintainer tags |
| `schema_version` | Corpus schema `$id` from [SCHEMA.json](SCHEMA.json) |
| `git_ref` | Short git commit at export time (empty outside a git checkout) |

Output file: `build/corpus/datasets/koskari-corpus/train.parquet`

### eval

Flat rows for corpus records that include `verify.eval`. Records verified
only at other phases (for example `read` or `expand`) are omitted.

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

Export all views (recommended before publish):

```bash
tox -qe regen-corpus -- --all-views
```

Export a single view:

```bash
tox -qe regen-corpus -- --view koskari-eval
tox -qe regen-corpus -- --view koskari-corpus
```

Makefile shortcuts:

```bash
make corpus-parquet-all
make corpus-parquet
make corpus-parquet-container
```

The container target builds `tools/corpus/Containerfile` and writes all views
into `build/corpus/datasets/` on the host.
