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

No dataset has been published yet. Transform tooling and dataset cards are
not implemented.

Each exported row should carry provenance (`corpus_id`, schema version).
