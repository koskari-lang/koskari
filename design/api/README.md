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
- **dunder-adapter.md**: Dunder adapter design — demand_sync and demand_next for Python slot interop, no-suspend sync bridge, slot wiring for KosType, internal push_or_await for OBJECT_* opcodes
- **demand-sync-await-violation-at-site.md**: Proposal — raise KoskariSyncViolation at the await site under demand path (in-eval TRY/unwind; includes DemandSyncGenerator; auto-await after CALL same as explicit AWAIT)
- **NAMESPACE-AND-REFS.md**: Namespace, Ref types, and related special forms
- **inspect-interop-kos-callables.md**: Python `inspect` (and related typing) interoperability for `KosFunction` and `KosBoundMethod` — `__signature__`, bound-method semantics, annotations, non-goals
- **import-system.md**: Import system for `.ki` files (meta_path, KoskariPathFinder, load_module_async)
- **lexer.md**: Lexer subpackage for lexical-only parsing, prompt_toolkit and LSP integration
- **pattern-matching.md**: Pattern matching and template expansion API for special-form handlers and future define-syntax; tagged-tuple representation, Python and Koskari consumption
- **matcher-hygiene-extensions.md**: MATCH role extensions for hygiene discovery; `BINDINGS` with `scope`/`scopes`; REST/EXPR as scope regions; `match(..., with_specs=True)` and role helpers; implemented `_collect_binding_symbols_matcher`
- **extra-table-and-opcode-metadata.md**: Extra operand table for bytecode — `KosExtra` rows, `EXTRA_*` opcode styles, four-field expansion schemas; enables fused opcodes (e.g. `CALL_FAST_ATTR_NAMES`) without variable-length instructions
- **stack-no-cow-proposal.md** (archived): Design proposal to remove copy-on-write semantics from KosStackBlock — see [archive/stack-no-cow-proposal.md](../archive/stack-no-cow-proposal.md)
