# Concepts

Cross-cutting topics that span many special forms and macros. Use these pages
when the [Glossary](../glossary.md) is too short and a single form page is too
narrow.

```{toctree}
:maxdepth: 1

bindings-and-refs
references-and-storage-indirection
brackets-and-call-convention
name-resolution-and-prefixes
demand-async-and-defer
choosing-an-async-construct
layout-and-s-expressions
fences
macro-matching-and-hygiene
pragma
code-sealing
opcode-caching
idiomatic-koskari
```

## Topics

| Page | Covers |
|------|--------|
| [Bindings and refs](bindings-and-refs.md) | Namespace, bind vs store, ref, deref, unpack |
| [References and storage indirection](references-and-storage-indirection.md) | Refs as location handles, `&` sugar, deref, `set!` target trap, `bind!`, by-ref params, vs Python |
| [Brackets and the call convention](brackets-and-call-convention.md) | `( )` = call / `[ ]` = data, bracket-pair type, `compiler/call-brackets` pragma |
| [Name resolution: runtime, compile-time, and layout](name-resolution-and-prefixes.md) | The three slots, `(foo …)` resolution, the two `+`, forcing a call with `call`, the `compile/`/`layout/` prefixes |
| [Demand, async, and defer](demand-async-and-defer.md) | Ambient async model, demand path, defer, on-demand? |
| [Choosing an async construct](choosing-an-async-construct.md) | Decision aid: `demand` / `define-demanding` vs `defer` vs `coroutine-fn` vs `as-coroutine` |
| [Layout and s-expressions](layout-and-s-expressions.md) | Layout vs s-expr surfaces, YAML/JSON mental model, three authoring forms |
| [Fences](fences.md) | Triple-backtick foreign literals, the compile-time transliterator model, when a format earns a fence |
| [Macro matching and hygiene](macro-matching-and-hygiene.md) | Pattern match at expand time, datum forms and wrappers |
| [Pragmas and feature settings](pragma.md) | Setting a pragma, the `reader/*` / `compiler/*` / `pragma/*` settings and their values |
| [Code sealing and arena packing](code-sealing.md) | Read-only bytecode, the `compiler/protect-code` / `compiler/pack-module` pragmas, arena pinning, per-expression sealing costs |
| [Attribute and method caching](opcode-caching.md) | Per-site monomorphic cache, the `compiler/cache-function` / `compiler/cache-expression` pragmas, what it accelerates, the 3.11+ dict-attribute floor |
| [Idiomatic Koskari](idiomatic-koskari.md) | Prefer the direct form: `item-slice`, dotted `set!`, `define-values`, `None?`, `unless`/`when` over Python-shaped assemblies |
