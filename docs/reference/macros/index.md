# Macros

This document describes the macros currently implemented in Koskari. Macros
expand at compile time before the compiler sees the result; the expanded forms
are then compiled as normal. For compiler special forms, see
[SPECIALS.md](SPECIALS.md).

Bootstrap loads these sources in order (`koskari/bootstrap/__init__.py`,
`_BUILTINS_SOURCES`):

1. `_define_syntax.ki` — meta-macros for `define-syntax` / `extend-syntax`
2. `_conditionals.ki` — `if`, `when`, `unless`, predicates
3. `_functions.ki` — coroutine/demand defines, decorators, composition
4. `_variables.ki` — assignment, refs, bindings, comprehensions helpers
5. `_class.ki` — classes, dataclasses, `super`
6. `_iter.ki` — `each`, `list-each`, `dict-each`
7. `basics.ki` — list accessors, `help`, macro expansion, compile/evaluate helpers

The merged module is exposed as `koskari.builtins`. Macros are available in every
module without import.

User-facing **`define-syntax`** is not a compiler special: it is expanded by
the **`compile/define-syntax`** macro in `_define_syntax.ki`. The compiler
lowers transformer specs via **`compile/syntax-rules`**, **`compile/macro-rules`**,
and related specials (see [SPECIALS.md](SPECIALS.md) § compile/define-syntax).

**Design:** [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md),
[MATCHER.md](MATCHER.md), [language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md).

### Block quasiquote and `:block:` layout expansion

Block backtick reads as **`(compile/quasiquote-block (:block: …))`**, rewritten to
**`(compile/quasiquote (:block: …))`**. `define-syntax` output is finalized through
**`koskari.syntax._finalize_macro_output`**, which calls **`apply_layout`** before
hygiene (see [SPECIALS.md](SPECIALS.md) § compile/quasiquote-block).

```{toctree}
:maxdepth: 1
:glob:

*
```
