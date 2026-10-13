# Corpus guide

## Purpose

The Koskari corpus describes language behaviour at subsystem boundaries:

- read (reader and layouts)
- expand (macros)
- eval (runtime, including `@`-marked complex checks)

Corpus records must not assert VM opcodes or bytecode shape. See
[SCHEMA.md](SCHEMA.md) eval and expand conventions.

Records are **not** shaped like ML training rows at source. Hugging Face and
similar exports are downstream transforms.

## Adding records

1. Define a record matching [SCHEMA.json](SCHEMA.json)
2. Append it as a new document (`---` separator) in the appropriate shard
   under `records/` (multi-document YAML; see [SCHEMA.md](SCHEMA.md))
3. Run corpus verification: `tox -qe py -- tests/corpus/ -m phase_corpus`

## Record quality rubric

A corpus record is acceptable when:

- **Source is pasteable** — idiomatic Koskari a reader could copy into docs or
  the REPL; avoid meaningless literal bodies used only as placeholders.
- **Verify matches stage** — macros include `expand` on a top-level macro form;
  layout features include `read`; runtime behavior uses `eval`.
- **Behavior is exercised** — eval source calls or reads the feature (slot
  access, invoke, predicate), not only returns a constructor.
- **Complex outcomes use `@`** — types, annotations, and similar values use
  marked encodings documented in [SCHEMA.md](SCHEMA.md); never English prose
  labels in verify strings.
- **References cite docs/** — link to the page the example could live on.

## Status

Schema documentation exists. Initial records live under `records/`:

| File | Focus |
|------|-------|
| [conditionals.yaml](records/conditionals.yaml) | Conditionals: compile/cond, switch, and, or; macros when, unless, ?, bool; layout if/when/unless |
| [variables.yaml](records/variables.yaml) | Bindings: var, global, define, define-global, let, letrec, refs, delq, parameters |
| [variables-invalid.yaml](records/variables-invalid.yaml) | Illegal binding forms (documented with verify.invalid) |
| [objects.yaml](records/objects.yaml) | Object operators, inplace, incr!/decr!, class, build-class |
| [objects-invalid.yaml](records/objects-invalid.yaml) | Illegal object-operator forms (arity errors, format spec pitfalls) |
| [classes.yaml](records/classes.yaml) | Layout class definitions with Python dunders, dataclass, staticmethod, classmethod, property, inheritance |
| [functions.yaml](records/functions.yaml) | lambda, function, def, define function shorthand, gensym-thunk, compose, decorate |
| [functions-invalid.yaml](records/functions-invalid.yaml) | Illegal gensym-thunk forms and compile-time placeholder errors |
| [syntax.yaml](records/syntax.yaml) | syntax, define-syntax, extend-syntax, rules/match, datum, pattern parsers |
| [specials.yaml](records/specials.yaml) | Other compiler special forms |
| [iter.yaml](records/iter.yaml) | Slicing, indexing, range, collection literals |

Records cite originating tests under `tests/compile/specials/` where
applicable; standalone seed examples use notes to describe the syntax focus.
Phase 10 corpus tests under `tests/corpus/` verify records against
[SCHEMA.json](SCHEMA.json).
