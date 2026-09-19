# Special forms

Specials are registered in the compiler's module namespace; the compiler
dispatches on the head symbol of a list form. The forms below are implemented in
`koskari/compile/specials/` and use helpers from `koskari/compile/helpers.py`
where applicable. Many handlers declare structure with **`koskari.matcher`**
patterns (`@special(..., patterns=...)`); see
[Macro matching and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.matcher`](../api/koskari-matcher.md) for pattern syntax, capture
shapes, and hygiene behaviour.

## Macros, reader macros, hygiene, and compiler optimizations

**Reader macros** — Parse-time character dispatch is implemented on
`koskari.reader.parser.Parser`: `set_macro_character`, `remove_macro_character`,
and default readers for quote, quasiquote, unquote, and unquote-splicing.

**Compile-time macros** — User-facing **`(define-syntax NAME SPEC)`** is expanded
by the **`compile/define-syntax`** macro in `koskari/bootstrap/_define_syntax.ki` (not a
`@special`). **`SPEC`** must begin with **`syntax-rules`**, **`macro-rules`**,
**`syntax-match`**, **`macro-match`**, **`special-rules`**, or **`special-match`**;
see **`compile/define-syntax`** and the **`compile/syntax-rules`**,
**`compile/macro-rules`**, **`compile/syntax-match`**, **`compile/macro-match`**,
**`compile/special-rules`**, and **`compile/special-match`** sections below.
**RULE** vs **MATCH** pattern languages share one transformer model; see
[Macro matching and hygiene](../../guide/macros-and-hygiene.md).

**Special-form transformers** — **`special-rules`** and **`special-match`** build
**`SpecialRulesTransformer`** values (`koskari/syntax.py`): rule procedures are
invoked with **`compiler`**, **`tc`**, and keyword arguments from the match
(unlike **`macro-rules`** / **`macro-match`**, which receive only keyword args).
Expansion uses **`unbox_macro`** (not **`unbox_macro_with_hygiene`**).

**Syntax boxes and hygiene** — **`koskari.syntax`** and **`koskari.hygiene`** own syntax-rule expansion and template hygiene; macro output runs **`koskari.reader.layouts.apply_layout`** (module namespace) before binding-symbol discovery and the hygiene / Datum finalize passes; see
[Macro matching and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.hygiene`](../api/koskari-hygiene.md) (**`Datum`**, **`DatumFrame`**, which matchers capture
and which register hygiene regions).

```{toctree}
:maxdepth: 1
:glob:

object-operators/index
*
```
