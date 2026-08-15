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

Publication runs from CI, so the Hub dataset follows the corpus records in the
repository.

## Configurations

The Hub dataset exposes several configurations, enumerated authoritatively in
the dataset card. The card lives at `tools/corpus/dataset_card.md` in the
repository and is uploaded as the Hub `README.md`.

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

## Export commands

Export all views (recommended before publish):

```bash
tox -qe regen-corpus -- --all-views
```

Export a single view:

```bash
tox -qe regen-corpus -- --view koskari-corpus
```

Makefile shortcuts:

```bash
make corpus-parquet-all
make corpus-parquet
make corpus-parquet-container
```

The container target runs the export in the shared `koskari-tools` image
(`make build-tools-container`) and writes all views into
`build/corpus/datasets/` on the host. Provenance differs there: the image
carries no git, so the `git_ref` column is empty in container-produced
output.
