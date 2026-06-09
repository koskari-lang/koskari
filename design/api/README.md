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
- **demand-wrapper.md**: `Demand` adapter type for Python interop; `demand_call` / `demand_apply` (rename from `demand_sync`); Ki bootstrap bindings `demand`, `demand-call`, `demand-apply`
- **demand-sync-await-violation-at-site.md** (archived): Historical proposal for in-eval KoskariSyncViolation at await under demand path — see [archive/demand-sync-await-violation-at-site.md](../archive/demand-sync-await-violation-at-site.md); implemented for `demand_sync_ex` / KosCallable `run_eval` in `koskari/lib/demand.c`
- **NAMESPACE-AND-REFS.md**: Namespace, Ref types, and related special forms
- **inspect-interop-kos-callables.md**: Python `inspect` (and related typing) interoperability for `KosFunction` and `KosBoundMethod` — `__signature__`, bound-method semantics, annotations, non-goals
- **import-system.md**: Import system for `.ki` files (meta_path, KoskariPathFinder, load_module_async)
- **koskari-scripts-packaging.md**: `koskari_scripts` entry points, `koskari.extras.setuptools` build plugin, and `koskari.launcher` runtime host for installed application commands (proposed)
- **lexer.md**: Lexer subpackage for lexical-only parsing, prompt_toolkit and LSP integration
- **pattern-matching.md**: Pattern matching and template expansion API for special-form handlers and define-syntax; tagged-tuple representation, Python and Koskari consumption. The **`MATCH(...)`** combinator documented there is how **specials** construct matchers; the **MATCH** macro pattern language (**`syntax-match`** / **`macro-match`**, lowered via `parse_explicit_rule_pattern`) targets the **same IR and fidelity**—Koskari surface syntax for expressive matchers. See [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) for **RULE** vs **MATCH** terminology
- **matcher-hygiene-extensions.md**: MATCH role extensions for hygiene discovery; `BINDINGS` with `scope`/`scopes`; REST/EXPR as scope regions; `match(..., with_specs=True)` and role helpers; implemented `_collect_binding_symbols_matcher`. Cross-ref: [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) (matcher-driven hygiene)
- **extra-table-and-opcode-metadata.md**: Extra operand table for bytecode — `KosExtra` rows, `EXTRA_*` opcode styles, four-field expansion schemas; enables fused opcodes (e.g. `CALL_FAST_ATTR_NAMES`) without variable-length instructions
- **stack-no-cow-proposal.md** (archived): Design proposal to remove copy-on-write semantics from KosStackBlock — see [archive/stack-no-cow-proposal.md](../archive/stack-no-cow-proposal.md)
