# Corpus guide

## Purpose

The Koskari corpus describes language behaviour at subsystem boundaries:

- read (reader and layouts)
- expand (macros)
- compile (author-visible outcomes such as return annotations or defined types)
- eval (runtime)

Corpus records must not assert VM opcodes or bytecode shape. See
[SCHEMA.md](SCHEMA.md) compile conventions.

Records are **not** shaped like ML training rows at source. Hugging Face and
similar exports are downstream transforms.

## Adding records

1. Define a record matching [SCHEMA.json](SCHEMA.json)
2. Place it under `records/` (format TBD: JSONL or directory tree)
3. Run corpus verification: `tox -qe py -- tests/corpus/ -m phase_corpus`

## Status

Schema documentation exists. Initial records live under `records/`:

| File | Focus |
|------|-------|
| [conditionals.jsonl](records/conditionals.jsonl) | Conditionals: compile/cond, switch, and, or; macros when, unless, ?, bool; layout if/when/unless |
| [variables.jsonl](records/variables.jsonl) | Bindings: define, define-global, let, letrec, refs, delq, parameters |
| [variables-invalid.jsonl](records/variables-invalid.jsonl) | Illegal binding forms (documented with verify.invalid) |
| [objects.jsonl](records/objects.jsonl) | Object operators, inplace, incr!/decr!, class, define-class |
| [classes.jsonl](records/classes.jsonl) | Layout class definitions with Python dunders, dataclass, staticmethod, classmethod, property, inheritance |
| [functions.jsonl](records/functions.jsonl) | lambda, function, define function shorthand, gensym-thunk, compose, decorate |
| [functions-invalid.jsonl](records/functions-invalid.jsonl) | Illegal gensym-thunk forms and compile-time placeholder errors |
| [syntax.jsonl](records/syntax.jsonl) | define-syntax, extend-syntax, rules/match, datum, pattern parsers |
| [specials.jsonl](records/specials.jsonl) | Other compiler special forms |
| [iter.jsonl](records/iter.jsonl) | Slicing, indexing, range, collection literals |

Records cite originating tests under `tests/compile/specials/` where
applicable; standalone seed examples use notes to describe the syntax focus.
Phase 10 corpus tests under `tests/corpus/` verify records against
[SCHEMA.json](SCHEMA.json).
