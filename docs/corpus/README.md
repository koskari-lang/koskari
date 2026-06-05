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
2. Place it under `records/` as JSONL, matching the existing topic files
3. Run corpus verification (runner not implemented yet)

## Status

Schema documentation exists. Initial records live under `records/`:

| File | Focus | Records |
|------|-------|---------|
| [conditionals.jsonl](records/conditionals.jsonl) | Conditionals: compile/cond, switch, and, or; macros when, unless, ?, bool | 33 |
| [variables.jsonl](records/variables.jsonl) | Bindings: define, define-global, let, letrec, refs, delq, parameters | 30 |
| [objects.jsonl](records/objects.jsonl) | Object operators, inplace, incr!/decr!, class, define-class | 41 |
| [functions.jsonl](records/functions.jsonl) | lambda, function, define function shorthand, gensym-thunk, compose, decorate | 21 |
| [syntax.jsonl](records/syntax.jsonl) | define-syntax, extend-syntax, rules/match, datum, pattern parsers | 32 |
| [specials.jsonl](records/specials.jsonl) | Other compiler special forms | 14 |

Records cite originating tests under `tests/compile/specials/` where
applicable. A corpus verification runner is **not implemented** yet; tests
under `tests/` currently serve as the live verifier.
