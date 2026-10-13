# compile/declare-exports

**Syntax:** `(declare-exports NAME ...)`

**Behaviour:** Expands to `(global __features__ #(...))` holding
the sorted, deduplicated names as strings. Each name is a feature stem.
When the module finishes loading, `__all__` is built from the stems as
the bindings that exist: the runtime name and its `compile/` and
`layout/` facets. A Python-side star import therefore sees only what is
bound, while [`use`](../specials/use.md) and `import-all-from` carry
every facet of a feature. A name written with a facet prefix exports
that one binding.

**Examples:**

```koskari
;; shapes.ki
(declare-exports area define-shape)

def area [w h] (* w h)

syntax define-shape:
  ...
```

```koskari
>>> (declare-exports greet shout)
>>> __features__
#("greet" "shout")
```

**See also:** [`use`](../specials/use.md),
[Modules and packages](../../guide/modules-and-packages.md).
