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
2. Append it as a new document (`---` separator) in the appropriate shard
   under `records/` (multi-document YAML; see [SCHEMA.md](SCHEMA.md))
3. Run corpus verification: `tox -qe py -- tests/corpus/ -m phase_corpus`

## Status

Schema documentation exists. Initial records live under `records/`:

| File | Focus |
|------|-------|
| [conditionals.yaml](records/conditionals.yaml) | Conditionals: compile/cond, switch, and, or; macros when, unless, ?, bool; layout if/when/unless |
| [variables.yaml](records/variables.yaml) | Bindings: define, define-global, let, letrec, refs, delq, parameters |
| [variables-invalid.yaml](records/variables-invalid.yaml) | Illegal binding forms (documented with verify.invalid) |
| [objects.yaml](records/objects.yaml) | Object operators, inplace, incr!/decr!, class, define-class |
| [classes.yaml](records/classes.yaml) | Layout class definitions with Python dunders, dataclass, staticmethod, classmethod, property, inheritance |
| [functions.yaml](records/functions.yaml) | lambda, function, define function shorthand, gensym-thunk, compose, decorate |
| [functions-invalid.yaml](records/functions-invalid.yaml) | Illegal gensym-thunk forms and compile-time placeholder errors |
| [syntax.yaml](records/syntax.yaml) | define-syntax, extend-syntax, rules/match, datum, pattern parsers |
| [specials.yaml](records/specials.yaml) | Other compiler special forms |
| [iter.yaml](records/iter.yaml) | Slicing, indexing, range, collection literals |

Records cite originating tests under `tests/compile/specials/` where
applicable; standalone seed examples use notes to describe the syntax focus.
Phase 10 corpus tests under `tests/corpus/` verify records against
[SCHEMA.json](SCHEMA.json).
