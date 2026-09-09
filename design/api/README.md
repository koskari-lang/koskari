# API Documentation

This directory documents the runtime API of Koskari, including types, functions, and built-in interfaces.

## Contents

- **package-layout.md**: Package structure, where symbols are defined (including all exceptions in `koskari.exceptions`), and import policy (import from submodules only; no top-level re-exports)
- **types.md**: Core data types (symbol, keyword, pair, nil, etc.)
- **builtins.md**: Built-in functions and utilities
- **defer-coroutine.md**: DeferCoroutine (`defer`) — single-shot await-handle, excluded from implicit auto-await
- **async-form.md**: **`async`** macro — concurrent fan-out/join on the asyncio loop (`defer` + `create-task` + `gather`; streaming **`each`** variant)
- **special-call-protocol.md**: Special call protocol (kos call) — install and tail_install slots, invocation entry points (kos_callable_call, kos_callable_tail_call), per-type behavior (Function, Expression, Continuation, Apply), native handler as frame evaluator, transfer goal model, flattening apply
- **runtime.md**: Runtime system and module loading
- **OBJECT-MODEL.md**: Koskari object model (KosType, class creation, synchronous `kostype_tp_call`, composite metaclass bases, method binding)
- **dunder-adapter.md**: Dunder adapter design — demand_sync and demand_next for Python slot interop, no-suspend sync bridge, slot wiring for KosType, `__init__` via `kostype_tp_call` (§5.4)
- **demand-wrapper.md**: `Demand` adapter type for Python interop; descriptor binding (`BoundMethod(Demand, obj)`); `demand_call` / `demand_apply`; Ki bootstrap bindings
- **demand-wrapper.md**: `Demand` adapter type for Python interop; descriptor binding (`BoundMethod(Demand, obj)`); `demand_call` / `demand_apply`; Ki bootstrap bindings. Implementation archived: [archive/demand-evalex-phase1.md](../archive/demand-evalex-phase1.md), [archive/demand-evalex-phase2.md](../archive/demand-evalex-phase2.md)
- **demand-sync-await-violation-at-site.md** (archived): Historical proposal superseded by [archive/demand-evalex-phase1.md](../archive/demand-evalex-phase1.md)
- **NAMESPACE-AND-REFS.md**: Namespace, Ref types, and related special forms
- **inspect-interop-kos-callables.md** (archived): Python `inspect` interop for `KosFunction` / `KosBoundMethod` — see [archive/inspect-interop-kos-callables.md](../archive/inspect-interop-kos-callables.md)
- **import-system.md**: Import system for `.ki` files (meta_path, KoskariPathFinder, load_module_async)
- **koskari-scripts-packaging.md** (archived): `koskari_scripts` entry points, `koskari.extras.setuptools` build plugin, and `koskari.launcher` runtime host — see [archive/koskari-scripts-packaging.md](../archive/koskari-scripts-packaging.md)
- **lexer.md**: Lexer subpackage for lexical-only parsing, prompt_toolkit and LSP integration
- **pattern-matching.md**: Pattern matching and template expansion API for special-form handlers and define-syntax; tagged-tuple representation, Python and Koskari consumption. The **`MATCH(...)`** combinator documented there is how **specials** construct matchers; the **MATCH** macro pattern language (**`syntax-match`** / **`macro-match`**, lowered via `parse_explicit_rule_pattern`) targets the **same IR and fidelity**—Koskari surface syntax for expressive matchers. See [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) for **RULE** vs **MATCH** terminology
- **matcher-hygiene-extensions.md**: MATCH role extensions for hygiene discovery; `BINDINGS` with `scope`/`scopes`; REST/EXPR as scope regions; `match(..., with_specs=True)` and role helpers; implemented `_collect_binding_symbols_matcher`. Cross-ref: [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) (matcher-driven hygiene)
- **extra-table-and-opcode-metadata.md** (archived): Extra operand table / EXTRA opcode style — see [archive/extra-table-and-opcode-metadata.md](../archive/extra-table-and-opcode-metadata.md)
- **stack-no-cow-proposal.md** (archived): Design proposal to remove copy-on-write semantics from KosSegment — see [archive/stack-no-cow-proposal.md](../archive/stack-no-cow-proposal.md)
