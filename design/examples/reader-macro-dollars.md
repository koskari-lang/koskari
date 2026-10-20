# Reader macro dollars example

## Status

Proposed — evaluate and implement.

## Purpose

Port the classic **dollars reader macro** demonstration into koskari as a
first-class example. The program registers **`$`** as a read-time prefix:
**`$100`** becomes **`(dollars 100)`** before macro expansion or compilation.
The example also defines a small **`dollars`** wrapper type and prints types,
representations, and string forms so readers can see all three phases clearly.

This is the canonical koskari introduction to **reader macros**: how they
differ from **`define-syntax`**, when to use them, and how module load binds
**`__reader__`** for registration.

## Source reference

Adapt from **`python-sibilant/examples/reader-macro.lspy`**: dollars class,
**`read-dollars`**, **`set-macro-character`**, and an **`example`** function
with **`when-main`**.

## Koskari surface

| Old spelling | Koskari spelling |
| ------------ | ---------------- |
| **`defclass`** | **`class`** |
| **`defun`** | **`def`** |
| **`defmacro print-example`** | **`define-syntax`** (compile-time demo in the same file) |
| **`set-macro-character`** | Thin bootstrap helper on **`__reader__`** (see below) |
| **`when-main`** | **`when-main`** (bootstrap) |

The example lives at **`examples/reader-macro/dollars.ki`** with a short
**`examples/reader-macro/README.md`** walkthrough.

## Reader helpers (bootstrap)

Expose load-time reader utilities so examples and user modules register macros
without calling Python methods directly:

| Helper | Behaviour |
| ------ | --------- |
| **`set-macro-character`** | **`(set-macro-character CHAR FN terminating: False)`** → **`(__reader__.add_macro_character CHAR FN …)`** during module load |
| **`read-atom`** | **`(read-atom stream)`** — read one atom from the current stream |
| **`float-like?`** (optional) | Predicate for the float-literal → decimal-literal munge in **`read-dollars`** |

**Home:** new bootstrap shard **`koskari/bootstrap/_reader.ki`**, registered in
**`_BUILTINS_SOURCES`** after **`_define_syntax.ki`**.

## Example behaviour

1. **`read-dollars`** reads the next atom from the stream.
2. When the atom looks like a float literal, rewrite it to decimal form
   (e.g. **`1.5`** → **`1.5d`**) so **`process-atom`** yields a decimal source
   form.
3. Return **`(dollars ,found)`** as the read result.
4. **`(define X $100)`** in source therefore binds **`X`** to a **`dollars`**
   instance at run time.

The file includes **`print-example`** (or equivalent) to show source, then
evaluated result, for both a bound name and a bare **`$200`** read.

## Tests

| Test | Phase | Coverage |
| ---- | ----- | -------- |
| **`tests/reader/test_reader_macro_dollars.py`** | `phase_infra` | Read **`$100`** → expected pair form; decimal munge when enabled |
| **`tests/reproducers/test_example_reader_macro.py`** (optional) | `phase_reproducers` | Run example **`when-main`** output |

## User documentation

- **`docs/patterns/recipes/reader-macro-dollars.md`** — tutorial prose and
  snippets; links **`examples/reader-macro/`** only (no internal design paths in
  user docs).
- **`docs/reference/macros/set-macro-character.md`** — reference for the
  bootstrap helper.

## Implementation order

1. **`_reader.ki`** helpers and bootstrap registration.
2. **`examples/reader-macro/dollars.ki`** + README.
3. Reader tests.
4. User docs.

## Related

- [../language/syntax.md](../language/syntax.md) — reader macro overview.
- [../SPECIALS.md](../SPECIALS.md) — reader macros vs compile-time macros.
- **`koskari/reader/parser.py`** — **`add_macro_character`**, **`read_atom`**.
- **`koskari/module.py`** — **`__reader__`** load facet.
