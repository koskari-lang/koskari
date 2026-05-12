# Language Features

This directory documents the language-level features of Koskari, including syntax, special forms, operators, and built-in constructs.

## Contents

- **conventions.md**: Recommended style for keywords and brackets (named args, flags, star/starstar, non-evaluation lists)
- **special-forms.md**: All special forms (compile-time constructs)
- **operators.md**: Operators and their compile-time optimizations
- **macros.md**: Macro system (reader macros and compile-time macros) — see [archive/macros.md](../archive/macros.md)
- **macro-hygiene-syntax-boxes.md**: Implemented syntax-box hygiene (`koskari.syntax_ng.Syntax`, `koskari.hygiene_ng`); archived full design note in [archive/macro-hygiene-syntax-boxes.md](../archive/macro-hygiene-syntax-boxes.md)
- **unified-matcher-transformer-model.md**: Shared Matcher IR and transformer pipeline for `define-syntax`, `macro-rules`, specials, and post-expand hygiene/unbox
- **syntax.md**: Syntax elements, literals, and parsing rules
- **layout-syntax.md**: Layout-based block syntax, layout heads, and reader model
- **layout-syntax-formal.md**: Formal EBNF specification of the Koskari Layout Syntax (lexical preprocessing, logical lines, grammar)
- **control-flow.md**: Control flow constructs (loops, conditionals, exceptions)
- **functions.md**: Function definition and calling conventions
- **async.md**: Async/await and coroutine features
- **generators.md**: Generator and yield features
- **RESOLUTION-MODEL.md**: Variable resolution model (proposed) — read vs write semantics, define-global, typo safety
