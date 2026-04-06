# Koskari Unified Matcher & Transformer Model

**Status:** Design proposal; not implemented. Describes the intended architecture
where declarative macros, procedural macros, and Python-defined specials share a
single matcher-driven pipeline. Complements concrete hygiene mechanics in
[macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md).

## Related documents

- **Syntax boxes / procedural hygiene**: [macro-hygiene-syntax-boxes.md](macro-hygiene-syntax-boxes.md)
- **Matcher hygiene metadata (current implementation)**: [matcher-hygiene-extensions.md](../api/matcher-hygiene-extensions.md)
- **Pattern matching API (tagged tuples, `koskari.matcher`)**: [pattern-matching.md](../api/pattern-matching.md)
- **Implemented compile-time macros**: [MACROS.md](../MACROS.md)
- **`compile/define-syntax` reference**: [SPECIALS.md](../SPECIALS.md) (compile/define-syntax)
- **Open work**: [TODO.md](../TODO.md) (Macro expansion; Procedural macro hygiene)
- **Historical macro notes**: [archive/macros.md](../archive/macros.md)

---

## Overview

Koskari's macro and syntax systems converge on a single underlying abstraction:

> A **transformer** is an ordered set of pattern rules. Each rule matches input source, produces named bindings, executes an outcome handler, then passes the result through hygiene and unboxing.

This model unifies:

* `define-syntax` (declarative)
* `define-macro` (procedural)
* Python-defined special forms

All three compile down to a shared **Matcher IR** and execute via the same pipeline.

---

## Core Pipeline

For any transformer invocation:

1. **Pattern Dispatch**

   * Rules are evaluated in order
   * First successful match wins

2. **Match Environment**

   * Produces a named capture map
   * Includes metadata (scope roles, repetition, boxing)

3. **Outcome Execution**

   * Receives match environment
   * Produces source form (possibly boxed)

4. **Hygiene Transform**

   * Uses matcher metadata + expansion preview
   * Applies gensym / binding isolation

5. **Unboxing / Splice**

   * Resolves syntax boxes
   * Produces final source form

---

## Matcher IR

The Matcher is the canonical representation of patterns.

### Primitive Matchers

* `SYMBOL(name)`
* `VALUE(name)`
* `LITERAL(sym)`
* `LITERAL_KW(kw)`
* `REST(name)`

### Decorated Matchers

* `FORMALS(name, scope=...)`
* `BINDING(name, scope=...)`
* `USE(name)`
* `TYPE_EXPR(name)`
* `ATTR_PATH(name)`

### Composition

* `MATCH(...)` → ordered sequence
* Optional future:

  * `OPTIONAL(...)`
  * `ONE_OF(...)`
  * `REPEAT(...)`

All surface syntaxes lower into this IR.

---

## Transformer Object

A transformer is a first-class object:

* `rules`: ordered list of pattern + outcome
* `matcher`: compiled matcher IR
* `metadata`: optional provenance / tags

### Rule Structure

Each rule contains:

* pattern (Matcher IR)
* optional guard / metadata
* outcome handler

---

## Surface Syntax

### Clause Forms

Two clause heads define pattern dialects:

#### 1. `match` — Minimal Pattern

Implicit matcher construction:

```scheme
match (if cond then else):
  (compile/cond ...)
```

Lowering:

* literals → `LITERAL`
* symbols → `VALUE`
* `...` → `REST`

#### 2. `rule` — Decorated Pattern

Explicit matcher semantics:

```scheme
rule (lambda name :: return_type formals body ...):
  ...
```

Or extended form (illustrative):

```scheme
rule (lambda
        [name :: SYMBOL]
        [return_type :: VALUE]
        [formals :: FORMALS scope: body]
        [body :: REST]):
  ...
```

The exact syntax is flexible, but must support:

* explicit matcher atom selection
* scope annotation
* binding roles

---

## define-syntax

Declarative transformer:

```scheme
define-syntax compile/if:
  rules:
    match (if cond then else):
      (compile/cond [cond then] [else: else])
```

Characteristics:

* template-style outcomes
* limited logic
* ergonomic default

---

## define-macro

Procedural transformer:

```scheme
define-macro compile/lambda:
  rules:
    rule (lambda name :: return_type formals body ...):
      ;; procedural expansion using match env
      ...
```

Characteristics:

* same matcher system
* outcome is code
* access to match bindings
* can inspect presence/absence of captures

---

## Outcome Contract

All outcomes share a unified contract:

Input:

* match environment (named captures)

Output:

* source form tree
* may include syntax boxes

No distinction between syntax/macro at this level.

---

## Hygiene Model

Hygiene is matcher-informed:

* matcher atoms declare binding roles (e.g. `FORMALS(scope=...)`)
* expansion is previewed
* symbols introduced by transformer are distinguished from captured ones
* gensym applied where needed

Future direction:

* reduce reliance on preview
* use matcher-declared binding metadata directly

---

## Syntax Boxes

Single box type:

* marks preserved source segments
* prevents premature transformation
* unboxed only at final phase

Used by procedural macros to mix:

* introduced structure
* original user forms

---

## Extension & Composition

Transformers are first-class and extensible.

### Operations

* `extend-transformer`
* `prepend-rules`
* `append-rules`
* `replace-rule`

### Example

```scheme
(define-syntax base-cond ...)

(define-syntax debug-cond
  (extend-syntax base-cond
    (prepend
      rule (cond [debug: x] body ...):
        ...)))
```

Properties:

* extension defaults to prepend
* preserves rule ordering
* provenance tracked for debugging

---

## Design Principles

1. **Single Matcher Engine**

   * All pattern matching compiles to one IR

2. **Unified Transformer Model**

   * Syntax, macros, and specials share structure

3. **Separation of Concerns**

   * Pattern → Outcome → Hygiene → Unbox

4. **Named Capture Environment**

   * All matches produce structured bindings

5. **Matcher-Driven Hygiene**

   * Binding semantics come from matcher, not inference

6. **Extensibility First**

   * Transformers are composable values

7. **Progressive Complexity**

   * `match` for simple cases
   * `rule` for advanced cases

---

## Open Questions

* Final surface syntax for decorated matcher atoms
* Whether `match`/`rule` naming should be swapped
* How much matcher algebra (OPTIONAL, OR, etc.) to expose
* How to surface debugging (which rule matched, why)

---

## Summary

Koskari’s macro system is best understood as:

> A matcher-driven transformation engine with multiple authoring frontends.

This provides:

* conceptual unity
* extensibility
* strong hygiene guarantees
* a path from simple syntax sugar to full procedural macros without changing the underlying model
