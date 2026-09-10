# Language Features

This directory documents the language-level features of Koskari, including syntax, special forms, operators, and built-in constructs.

## Contents

- **conventions.md**: Recommended style for keywords and brackets (named args, flags, star/starstar, non-evaluation lists)
- **special-forms.md**: All special forms (compile-time constructs)
- **[archive/operators.md](../archive/operators.md)** (archived): python-sibilant operator notes; the operator surface is documented for users in `docs/reference/specials/object-operators/index.md`
- **macros.md**: Macro system (reader macros and compile-time macros) — see [archive/macros.md](../archive/macros.md)
- **[archive/macro-hygiene-syntax-boxes.md](../archive/macro-hygiene-syntax-boxes.md)** (archived): syntax-box hygiene design (`koskari.syntax.Syntax`, `koskari.hygiene`); implemented
- **unified-matcher-transformer-model.md**: Shared Matcher IR and transformer pipeline for `define-syntax`, `macro-rules`, specials, and post-expand hygiene/unbox
- **syntax.md**: Syntax elements, literals, and parsing rules
- **Layout syntax (user docs):** [docs/reference/reader-layout.md](../docs/reference/reader-layout.md), [docs/reference/concepts/layout-and-s-expressions.md](../docs/reference/concepts/layout-and-s-expressions.md); archived drafts in [archive/layout-syntax.md](../archive/layout-syntax.md), [archive/layout-syntax-formal.md](../archive/layout-syntax-formal.md)
- **flat-layout-default.md**: Default **`layout/block`** flat splice and read pipeline
- **control-flow.md**: Control flow constructs (loops, conditionals, exceptions)
- **functions.md**: Function definition and calling conventions
- **async.md**: Async/await and coroutine features
- **generators.md**: Generator and yield features
- **RESOLUTION-MODEL.md**: Variable resolution model (proposed) — read vs write semantics, define-global, typo safety
