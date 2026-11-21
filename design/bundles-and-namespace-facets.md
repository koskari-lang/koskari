# Koskari Design: Bundles and Namespace Facets

## Status

Exploratory design.

**Settled: the import seam.** The `use` special and its `IMPORT_USE`
opcode were implemented with their contract stated as "obtain the bindings a
feature names, and store them". Today the obtain step is the default
facet probe -- the stem itself, `compile/{stem}`, `layout/{stem}`.
When bundles are implemented, a module that declares an installer under a
reserved name replaces that step: the opcode invokes the installer
upon the module, and the installer returns a name-to-value mapping
stored exactly as probe hits are. Bundles therefore need no new
opcode and no new import surface.

Everything below -- facades, trampolines, activation scopes -- is one
possible bundle solution, not a settled direction.

## Summary

Introduce **bundles** as first-class namespace facet objects that can be activated during compilation and runtime resolution. Bundles provide a structured way to deliver a cohesive feature surface without polluting the module namespace.

A bundle:

* contains a namespace of real implementation bindings
* exposes a controlled public API via an `exports` list
* may be activated via `with-bundle` (or equivalent compiler mechanism)
* participates in both compile-time operator resolution and runtime name lookup

Exports are typically **forwarding facades** that activate the bundle context and redispatch to bundle-local implementations.

This mechanism allows features (e.g., SQL integration, data DSLs, language extensions) to ship as cohesive namespaces while keeping the module's top-level namespace small and explicit.

## Motivation

Koskari intentionally uses a **single namespace model**:

* the same objects exist for compile-time and runtime
* naming conventions such as `compile/` determine compiler behavior
* objects are still inspectable at runtime

However, complex features often require:

* multiple cooperating helpers
* specialized operators or specials
* runtime helpers
* internal support objects

Requiring users to import many names explicitly is fragile and leaks implementation details.

Bundles solve this by allowing a feature to ship as a **namespace deliverable** that is activated explicitly when needed.

## Core Concept

A bundle is a namespace container with three parts:

1. **Namespace** – the actual implementation bindings.
2. **Exports list** – names exposed to outer modules.
3. **Activation mechanism** – a way to temporarily push the bundle namespace into the resolution chain.

Example conceptual structure:

```
bundle koskari.fences.sql
  namespace:
    fence/sql
    sql/statement
    sql/query-one
    sql/internal-bind

  exports:
    fence/sql
    sql/statement
```

Only names listed in `exports` appear in the outer namespace.

All other names remain accessible only when the bundle is active.

## Export Forwarding

Exports are typically **forwarding entries**, not direct aliases.

An exported symbol captures the bundle instance and redispatches compilation under that bundle context.

Conceptually:

```
fence/sql (export facade)
  push bundle koskari.fences.sql
  redispatch to bundle-local fence/sql
```

The real implementation lives inside the bundle namespace.

This ensures that internal helpers remain available during expansion without leaking into module scope.

## Compiler Trampoline Model

Export facades may operate as compiler trampolines rather than source-level rewrites.

Instead of expanding to a visible wrapper form such as:

```
(with-bundle B (fence/sql ...))
```

the export object may directly perform:

1. `compiler.push_bundle(bundle)`
2. redispatch compilation of the source form
3. `compiler.pop_bundle()`

This avoids generating artificial wrapper syntax and directly reflects the intended compiler behavior.

## Bundle Activation

Bundles are activated using a scoped mechanism conceptually similar to `let`.

Example conceptual form:

```
(with-bundle koskari.fences.sql
  BODY)
```

Activation pushes the bundle namespace into the resolution chain for the duration of `BODY`.

Bundles affect both:

* **compile-time resolution** (specials, macros, operator handlers)
* **runtime name lookup** (ordinary bindings)

## Name Resolution Model

When a bundle is active, name resolution follows this order:

1. local lexical bindings
2. active bundle namespaces (nearest first)
3. enclosing module / import scope
4. builtins

This ensures:

* local code can still shadow bundle names
* bundle helpers are available during expansion
* module-level definitions remain accessible

## Compile-Time Resolution

The compiler's operator/special resolver should also consult active bundle namespaces.

Conceptually:

```
resolve_operator(name):
  check active bundles
  check module namespace
  check builtins
```

This allows bundles to supply:

* special forms
* compile-time operators
* fence handlers
* layout handlers

without requiring them to exist in module scope.

## Runtime Semantics

Bundles are not purely compile-time constructs.

Because Koskari names resolve to closure references, bundle bindings may be captured by closures defined within the bundle scope.

Example:

```
(with-bundle sql
  (lambda () (sql/statement ...)))
```

The closure captures the bundle-provided binding just like any other namespace binding.

This behavior makes bundle scopes act like ordinary lexical namespace layers.

## Internal vs Public Names

Bundles should distinguish between:

* **exported names** – intended entry points
* **internal helpers** – only available when bundle is active

Internal helpers are not automatically visible in outer module scope.

They are accessed only when the bundle namespace is active.

This prevents namespace pollution while allowing complex internal machinery.

## Documentation Requirements

Bundles that participate in namespace resolution must document their surfaces clearly.

Each bundle should describe:

* exported names
* internal helpers
* compile-time operators
* intended direct-use names
* expansion-only names

Example documentation structure:

```
Bundle: koskari.fences.sql

Exports:
  fence/sql
  sql/statement

Internal helpers:
  sql/internal-bind
  sql/internal-row

Compile-time operators:
  fence/sql

Runtime helpers:
  sql/statement
```

Explicit documentation is necessary because bundles modify the visible namespace within their scope.

## Introspection

Bundles may optionally provide machine-readable metadata describing:

* exported names
* internal members
* binding types

This allows tooling to support:

* REPL inspection
* IDE completion
* documentation generation

## Ambiguity Rules

If multiple bundles define the same name, resolution follows the bundle stack order.

Nearest (most recently activated) bundle takes precedence.

Ambiguities within the same precedence level should produce an error.

## Design Advantages

Bundles provide several benefits:

* structured feature delivery
* reduced namespace pollution
* explicit feature activation
* closure-safe expansion behavior
* compatibility with Koskari's single namespace model

They allow complex language extensions to remain self-contained without requiring large numbers of manual imports.

## Example Workflow

A module imports a bundle export:

```
(import koskari.fences.sql)
```

The module gains access to exported entry points such as:

```
fence/sql
sql/statement
```

When `fence/sql` is invoked, the export trampoline activates the bundle and redispatches to the bundle-local implementation.

Internal helpers become visible automatically during expansion.

## Open Questions

### Bundle Creation Syntax

The precise syntax for defining bundles remains unspecified.

### Bundle Import Syntax

Settled: `(use MODULE FEATURE ...)` is the import surface. The open
question is only the installer side -- the reserved declaration name
and the installer's calling convention.

### Nested Bundle Composition

Bundles may eventually support importing or composing other bundles.

### Static Analysis

Tooling may need to understand bundle activation to correctly analyze code.

## Summary

Bundles are namespace facet objects that allow Koskari features to deliver a cohesive set of bindings without exposing all implementation details in module scope.

Exported names act as contextual entry points that activate the bundle environment and redispatch compilation.

This mechanism preserves Koskari's single namespace philosophy while enabling complex language extensions to remain self-contained.
