# import-all-from
**Symbol:** `import-all-from`

**Syntax:** `(import-all-from MODULE)`

`MODULE` follows the same dotted-name and relative-import rules as `import`.
There is no member list, no rename, and no mixing with named imports -- to
combine a wildcard and named imports, write two statements (as in Python).

**Use-case:** Bulk-bind a module's public names into the current namespace,
mirroring Python's `from MODULE import *`.

Name selection follows Python:

- If the module defines `__all__` (a sequence of strings), exactly those names
  are bound. A non-string entry raises `TypeError`.
- Otherwise every name from `dir(MODULE)` that does not start with an
  underscore is bound.

Existing bindings of the same name are overwritten.

**Restriction:** `import-all-from` is allowed **only at module top level**,
matching Python 3. Inside a function, lambda, `let`, or class body it is a
compile error.

**Statement value:** `None`.

**Examples:**

```koskari
;; Bind all public names of os.path
(import-all-from os.path)

;; From a relative package
(import-all-from .util)
```
