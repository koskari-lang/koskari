# import
**Symbol:** `import`

**Syntax:**

- Plain: `(import MODULE)` -- bind the root package
- Member: `(import [MODULE as: BIND])` -- bind BIND to the leaf module
- Relative: `(import .MODULE)`, `(import ..MODULE)` (leading dots)

`MODULE` is an unquoted symbol whose printed name is the module name. Leading
dots indicate a relative import (`level = number of dots`).

**Use-case:** Import a module and bind it in the current namespace, mirroring
Python's `import` statement.

- **Plain dotted import binds the root**, Python-style: `(import foo.bar)`
  binds `foo`, with `foo.bar` reachable as an attribute (`foo.bar`). The whole
  dotted chain is imported.
- **The bracketed member form binds the leaf**: `(import [foo.bar as: fbar])`
  binds `fbar` to the `foo.bar` submodule. The member uses the same
  `[TARGET as: BIND]` grammar as `import-from`.
- **Relative imports are always leaf-style**: `(import .foo)` binds the leaf
  `foo`. A bare relative import (dots only, no name) requires the member form
  and starts at two dots -- a single lone dot is the reader's dotted-pair
  marker and cannot be written in list syntax. To bind from the immediate
  parent package, name the module: `(import .x)`.

Inline `as:` (`(import foo as: bar)`) is **not** supported; use the bracketed
member form.

**Statement value:** `None`.

**Examples:**

```koskari
;; Plain dotted import -- binds 'foo' (root); foo.bar reachable as attribute
(import foo.bar)

;; Simple import -- binds 'os'
(import os)

;; Member form -- binds 'osp' to the leaf os.path
(import [os.path as: osp])

;; Relative import (one dot) -- binds leaf 'sibling'
(import .sibling)

;; Two-dot relative import -- binds leaf 'parent'
(import ..parent)

;; Bare relative (dots only) -- needs the member form
(import [.. as: parent-pkg])
```
