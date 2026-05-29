# KOS_OP_IMPORT_REL

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of a `(level:int, name:str|None)` tuple
  (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., module]`. Perform a relative module import and push
  the leaf module. Semantics otherwise identical to IMPORT.
- **Invariants**: `cpool[arg]` must be a 2-tuple `(int, str|None)`.
- **`level`**: Number of leading dots in the import expression (e.g. 1 for
  `(import .foo)`, 2 for `(import ..foo)`).
- **`name`**: Module name after the dots, or `None` for a bare relative import
  (e.g. `(import .)` which imports the enclosing package itself).
- **Anchor resolution**: The module globals namespace (outermost parent of the
  current frame's namespace) is passed as the `globals` argument to
  `PyImport_ImportModuleLevelObject` so Python can read
  `__name__` and `__package__` to resolve the anchor package. This is identical to
  what Python's own relative import mechanism does.
- **Compiler form:** Pre-compile arg = `(level, name)` Python tuple; post-compile
  arg = constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT_REL.c`

**Example:** `(import .foo)` inside package `mypkg` — load `mypkg.foo` and bind as `foo`.
Pool: 0 = `(1, "foo")`, 1 = `"foo"`, 2 = None.

| index | opcode     | arg | arg view       | comment |
|------:|------------|-----|----------------|---------|
| 0     | IMPORT_REL | 0   | 0 ((1, "foo")) | stack: [..., mypkg.foo] |
| 1     | VAR_BIND   | 1   | 1 ("foo")      | bind "foo" = module; stack: [...] |
| 2     | CONST      | 2   | 2 (None)       | statement value |
| 3     | RETURN     | 1   | 1              | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
