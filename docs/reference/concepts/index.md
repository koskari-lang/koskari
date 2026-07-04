# Concepts

Cross-cutting topics that span many special forms and macros. Use these pages
when the [Glossary](../glossary.md) is too short and a single form page is too
narrow.

```{toctree}
:maxdepth: 1

bindings-and-refs
references-and-storage-indirection
brackets-and-call-convention
demand-async-and-defer
layout-and-s-expressions
macro-matching-and-hygiene
pragma
```

## Topics

| Page | Covers |
|------|--------|
| [Bindings and refs](bindings-and-refs.md) | Namespace, bind vs store, ref, deref, unpack |
| [References and storage indirection](references-and-storage-indirection.md) | Refs as location handles, `&` sugar, deref, `set!` target trap, `bind!`, by-ref params, vs Python |
| [Brackets and the call convention](brackets-and-call-convention.md) | `( )` = call / `[ ]` = data, bracket-pair type, `compiler/call-brackets` pragma |
| [Demand, async, and defer](demand-async-and-defer.md) | Ambient async model, demand path, defer, on-demand? |
| [Layout and s-expressions](layout-and-s-expressions.md) | Layout vs s-expr surfaces, YAML/JSON mental model, three authoring forms |
| [Macro matching and hygiene](macro-matching-and-hygiene.md) | Pattern match at expand time, datum forms and wrappers |
| [Pragmas and feature settings](pragma.md) | Setting a pragma, the `reader/*` / `compiler/*` / `pragma/*` settings and their values |
