# compile/declare-exports
**Syntax:** **`(declare-exports name …)`**

**Behaviour:** **`macro-rules`** → **`(define-global __features__ #(…))`**
with sorted, deduplicated string names. Each name is a **feature stem**: at
the end of the module load, `__all__` is materialized from the stems as the
binding keys that actually exist — the runtime name and the `compile/` and
`layout/` facets of each — so Python-side star imports stay honest while
[`use`](../specials/use.md) and `import-all-from` carry every facet. A name
written with a facet prefix is a literal single-binding export.
