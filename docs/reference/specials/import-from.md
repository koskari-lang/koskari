# import-from
**Symbol:** `import-from`

**Syntax:**

- `(import-from MODULE NAME ...)` — import named attributes from MODULE
- `(import-from MODULE [NAME as: BIND] ...)` — with per-name renames
- `(import-from .MODULE NAME ...)` — relative module, named attributes
- Mix of plain symbols and `[NAME as: BIND]` forms is allowed.

`MODULE` is an unquoted symbol (same leading-dot convention as `import`). Each
`NAME` is a symbol used as both the attribute name and the binding name unless
a `[NAME as: BIND]` member overrides the binding.

**Use-case:** Import specific names from a module into the current namespace.
Mirrors Python's `from MODULE import NAME [as BIND], ...`.

A member that names a not-yet-imported submodule is imported on demand, so
`(import-from pkg sub)` works even when `pkg`'s `__init__` does not import
`sub` (matching CPython's `from pkg import sub`).

**Statement value:** `None`.

**Examples:**

```koskari
;; Single name from absolute module
(import-from os path)

;; Multiple names
(import-from os path sep getcwd)

;; With a rename
(import-from os [path as: osp] sep)

;; From relative module
(import-from .util helper format)

;; Relative with rename
(import-from .util [helper as: h])
```
