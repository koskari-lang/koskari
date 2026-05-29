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

Schema documentation exists. Initial records live under `records/`:

| File | Focus | Records |
|------|-------|---------|
| [specials.jsonl](records/specials.jsonl) | Compiler special forms | 43 |

Records cite originating tests under `tests/compile/specials/` where
applicable. A corpus verification runner is **not implemented** yet; tests
under `tests/` currently serve as the live verifier.
