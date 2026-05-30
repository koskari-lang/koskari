# import
**Symbol:** `import`

**Syntax:**

- Absolute: `(import MODULE)` or `(import MODULE as: ALIAS)`
- Relative: `(import .MODULE)` or `(import ..MODULE)` (leading dots, optional alias)
- Dotted: `(import foo.bar.baz)` or `(import foo.bar.baz as: baz)`

`MODULE` is an unquoted symbol whose printed name is used as the module name.
Leading dots on the symbol indicate a relative import (`level = number of dots`).

**Use-case:** Import a module and bind it in the current namespace. Mirrors
Python's `import` statement. The bound name is the leaf component of the dotted
name (e.g. `baz` for `foo.bar.baz`) unless overridden with `as:`.

**Behaviour:**

- Absolute import (no leading dot): emits `IMPORT cpool["dotted.name"]`, then
`VAR_BIND cpool["leaf"]` (or the alias name), then `CONST None` (statement
result).
- Relative import (one or more leading dots): emits `IMPORT_REL cpool[(level, name)]`, then `VAR_BIND cpool["leaf_or_alias"]`, then `CONST None`.
- Bare relative import (only dots, no module name, e.g. `(import .)`): name
portion is `None` in the pool tuple; binds the package under the leaf of the
dots (or an explicit alias).
- `as:` keyword overrides the binding name.

**Opcodes emitted:** `IMPORT` (absolute) or `IMPORT_REL` (relative), followed
by `VAR_BIND` and `CONST None`.

**Implementation:** `koskari/compile/specials/imports.py` — `special_import`

**Examples:**

```koskari
;; Absolute import — binds 'os' in current namespace
(import os)

;; Dotted absolute import — binds 'path' (leaf of os.path)
(import os.path)

;; Absolute import with alias
(import os.path as: osp)

;; Relative import (one dot)
(import .sibling)

;; Relative import with alias
(import .sibling as: s)

;; Two-dot relative import
(import ..parent)
```


