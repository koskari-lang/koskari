# API Documentation

This directory documents the runtime API of Koskari, including types, functions, and built-in interfaces.

## Contents

- **package-layout.md**: Package structure, where symbols are defined (including all exceptions in `koskari.exceptions`), and import policy (import from submodules only; no top-level re-exports)
- **types.md**: Core data types (symbol, keyword, pair, nil, etc.)
- **builtins.md**: Built-in functions and utilities
- **defer-coroutine.md**: DeferCoroutine (`defer`) — single-shot await-handle, excluded from implicit auto-await
- **async-form.md**: **`async`** macro — concurrent fan-out/join on the asyncio loop (`defer` + `create-task` + `gather`; streaming **`each`** variant)
- **runtime.md**: Runtime system and module loading
- **repr-detail.md**: Proposal: `kos_repr` with an identity level and a detail level, and the hand-off rule between them
- **demand-sync-await-violation-at-site.md** (archived): Historical proposal superseded by [archive/demand-evalex-phase1.md](../archive/demand-evalex-phase1.md)
- **NAMESPACE-AND-REFS.md**: Namespace, Ref types, and related special forms
- **inspect-interop-kos-callables.md** (archived): Python `inspect` interop for `KosFunction` / `KosBoundMethod` — see [archive/inspect-interop-kos-callables.md](../archive/inspect-interop-kos-callables.md)
- **import-system.md**: Import system for `.ki` files (the sys.path_hooks directory hook, KoskariSourceFileLoader, load_module_async)
- **koskari-scripts-packaging.md** (archived): `koskari_scripts` entry points, `koskari.extras.setuptools` build plugin, and `koskari.launcher` runtime host — see [archive/koskari-scripts-packaging.md](../archive/koskari-scripts-packaging.md)
- **lexer.md**: Lexer subpackage for lexical-only parsing, prompt_toolkit and LSP integration
- **pattern-matching.md**: Pattern matching and template expansion API for special-form handlers and define-syntax; tagged-tuple representation, Python and Koskari consumption. The **`MATCH(...)`** combinator documented there is how **specials** construct matchers; the **MATCH** macro pattern language (**`syntax-match`** / **`macro-match`**, lowered via `parse_explicit_rule_pattern`) targets the **same IR and fidelity**—Koskari surface syntax for expressive matchers. See [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) for **RULE** vs **MATCH** terminology
- **[../archive/matcher-hygiene-extensions.md](../archive/matcher-hygiene-extensions.md)** (archived): MATCH role extensions for discovery-by-pattern hygiene, superseded by the marks in [../MATCHER.md](../MATCHER.md) § Hygiene model
- **extra-table-and-opcode-metadata.md** (archived): Extra operand table / EXTRA opcode style — see [archive/extra-table-and-opcode-metadata.md](../archive/extra-table-and-opcode-metadata.md)
- **stack-no-cow-proposal.md** (archived): Design proposal to remove copy-on-write semantics from KosSegment — see [archive/stack-no-cow-proposal.md](../archive/stack-no-cow-proposal.md)
