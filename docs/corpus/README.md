# Corpus guide

## Purpose

The Koskari corpus describes language behaviour at subsystem boundaries:

- read (reader and layouts)
- expand (macros)
- compile (specials)
- eval (runtime)

Records are **not** shaped like ML training rows at source. Hugging Face and
similar exports are downstream transforms.

## Adding records

1. Define a record matching [SCHEMA.json](SCHEMA.json)
2. Place it under `records/` (format TBD: JSONL or directory tree)
3. Run corpus verification (runner not implemented yet)

## Status

Schema documentation exists; records and automated verification are **not
started**. Tests under `tests/` currently serve as the implicit conformance
corpus.
