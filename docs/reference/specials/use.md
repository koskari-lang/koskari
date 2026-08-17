# use
**Symbol:** `compile/use`

**Syntax:**

- `(use MODULE)` — bring in every exported feature of MODULE
- `(use MODULE STEM ...)` — bring in the named features
- `(use MODULE [STEM as: BIND] ...)` — with per-feature renames
- `(use .MODULE STEM ...)` — relative module, same leading-dot convention
  as `import`

**Use-case:** The koskari-native import. A stem names a *feature*, and every
facet of it imports together: the runtime binding `stem`, the transformer
`compile/stem`, and the layout handler `layout/stem` — whichever of them the
module defines. Where `import-from` moves one binding per member,
`use` moves one *feature* per member, so a macro and its runtime sibling
never separate.

A rename substitutes the whole family: `[tacos as: burritos]` binds
`burritos`, `compile/burritos`, and `layout/burritos` from the source
feature's facets, so the renamed feature's form syntax and block syntax
follow the new name together.

A stem written with a facet prefix is **literal** — `(use mod
compile/tacos)` imports that one binding exactly, the escape hatch for
addressing a single facet.

**Degradation:** on a module with no facet keys — any ordinary Python
module — each stem resolves exactly as `import-from` would, including the
on-demand submodule fallback. `(use os.path join)` and
`(import-from os.path join)` are equivalent, so `use` is always safe to
write.

**The bare form** `(use MODULE)` is equivalent to
`(import-all-from MODULE)`: a koskari module's
[`declare-exports`](../macros/compile-declare-exports.md) names feature
stems, and at the end of its load `__all__` is materialized as the binding
keys those stems actually resolve to — so the star import already carries
every feature's facets.

**Errors:** a per-facet miss is normal; a stem resolving *no* facet raises
`ImportError` naming the spellings tried:

```
module mod has no feature tacos (tried tacos, compile/tacos, layout/tacos)
```

A real failure inside an on-demand submodule import propagates as itself,
exactly as `import-from` reports it.

**Restriction:** module top level only, like `import-all-from`. Inside a
nested scope the compile-time facets could not take effect anyway; use
`import-from` there for runtime values, or name a facet explicitly.

**Statement value:** `None`.

**Examples:**

```koskari
;; a feature that is both a macro and a runtime value
(use tacotruck tacos)
(tacos)             ; the macro expands
(map tacos fillings) ; the runtime sibling as a value

;; rename the family
(use tacotruck [tacos as: burritos])

;; everything the module exports
(use tacotruck)

;; exactly one facet
(use tacotruck compile/tacos)

;; a Python module: behaves as import-from
(use os.path join)
```

## Related

- [import-from](import-from.md) — one binding per member, exact Python
  semantics.
- [import-all-from](import-all-from.md) — Python's `from M import *`.
- [Name resolution](../concepts/name-resolution-and-prefixes.md) — the
  facet model `use` imports across.
