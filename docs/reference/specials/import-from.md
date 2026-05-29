# import-from
**Symbol:** `import-from`

**Syntax:**

- `(import-from MODULE NAME ...)` — import named attributes from MODULE
- `(import-from MODULE (NAME as: ALIAS) ...)` — with per-name aliases
- `(import-from .MODULE NAME ...)` — relative module, named attributes
- Mix of plain names and `(NAME as: ALIAS)` forms is allowed.

`MODULE` is an unquoted symbol (same leading-dot convention as `import`).
Each `NAME` is a symbol whose string value is used as both the attribute name
and the binding name unless `as: ALIAS` overrides the binding.

**Use-case:** Import specific names from a module into the current namespace.
Mirrors Python's `from MODULE import NAME [as ALIAS], ...`.

**Behaviour:**

1. Emit `IMPORT cpool["name"]` (absolute) or `IMPORT_REL cpool[(level, name)]`
  (relative) to push the module onto the stack.
2. For each `(attr, binding)` pair, emit `IMPORT_FROM cpool["attr"]` (PEEK TOS;
  push attribute value), then `VAR_BIND cpool["binding"]` (bind and pop).
3. Emit `POP 1` to discard the module (compensating for IMPORT_FROM's
  peek-not-pop behaviour).
4. Emit `CONST None` (statement result).

The `POP 1` at step 3 is **always** emitted by `helper_import_from_sequence()`
regardless of how many names are imported. This is the structural guarantee
that keeps IMPORT_FROM's stack invariant sound.

**Opcodes emitted:** `IMPORT` or `IMPORT_REL`, then for each name: `IMPORT_FROM`

- `VAR_BIND`, then `POP 1`, then `CONST None`.

**Implementation:** `koskari/compile/specials/imports.py` — `special_import_from`,
`helper_import_from_sequence`

**Examples:**

```scheme
;; Single name from absolute module
(import-from os path)

;; Multiple names
(import-from os path sep getcwd)

;; With aliases
(import-from os (path as: osp) sep)

;; From relative module
(import-from .util helper format)

;; Relative with alias
(import-from .util (helper as: h))
```


