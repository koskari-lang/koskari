# API Documentation

This directory documents the runtime API of Koskari, including types, functions, and built-in interfaces.

## Contents

- **package-layout.md**: Package structure, where symbols are defined (including all exceptions in `koskari.exceptions`), and import policy (import from submodules only; no top-level re-exports)
- **types.md**: Core data types (symbol, keyword, pair, nil, etc.)
- **builtins.md**: Built-in functions and utilities
- **defer-coroutine.md**: DeferCoroutine (`defer`) — single-shot await-handle, excluded from implicit auto-await
- **special-call-protocol.md**: Special call protocol (kos call) — install and tail_install slots, invocation entry points (kos_callable_call, kos_callable_tail_call), per-type behavior (Function, Expression, Continuation, Apply), native handler as frame evaluator, transfer plan model, flattening apply
- **runtime.md**: Runtime system and module loading
- **OBJECT-MODEL.md**: Koskari object model (KosType, KosObject, class creation, awaitable construction, C.new sync adapter)
- **NAMESPACE-AND-REFS.md**: Namespace, Ref types, and related special forms
- **import-system.md**: Import system and module resolution _(not yet written; import system design is pending)_
- **lexer.md**: Lexer subpackage for lexical-only parsing, prompt_toolkit and LSP integration
- **pattern-matching.md**: Pattern matching and template expansion API for special-form handlers and future define-syntax; tagged-tuple representation, Python and Koskari consumption
- **matcher-hygiene-extensions.md**: Proposed MATCH role extensions (`BODY`, `BIND_SYM`, `BIND_BINDINGS`, `BIND_FORMALS`) for generic hygiene discovery driven by special-declared matcher metadata; replaces hardcoded binding-form knowledge in the expander
