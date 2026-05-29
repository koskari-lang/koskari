# KOS_OP_IMPORT
- **Style**: CONST. Arg = constant pool index of module name string (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., module]`. Perform an absolute module import and push
  the **leaf** module. For dotted names (e.g. `"os.path"`), the leaf component
  (`os.path`) is pushed, not the root (`os`). This matches `importlib.import_module`
  semantics and differs from bare `__import__()`.
- **Invariants**: `cpool[arg]` must be a str.
- **Leaf behaviour**: Achieved by passing the module-level sentinel
  `_import_fromlist = ("*",)` as `fromlist` to `PyImport_ImportModuleLevelObject`.
  `_import_fromlist` is initialised once in `koskari_eval_init` and cleared in
  `koskari_eval_clear`.
- **Compiler form:** Pre-compile arg = module name string (e.g. `"os"` or
  `"os.path"`); post-compile arg = constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT.c`

**Example:** `(import os.path)` — load `os.path` and bind as `path`.
Pool: 0 = `"os.path"`, 1 = `"path"`, 2 = None.

| index | opcode   | arg | arg view       | comment |
|------:|----------|-----|----------------|---------|
| 0     | IMPORT   | 0   | 0 ("os.path")  | stack: [..., os.path] |
| 1     | VAR_BIND | 1   | 1 ("path")     | bind "path" = module; stack: [...] |
| 2     | CONST    | 2   | 2 (None)       | statement value; stack: [..., None] |
| 3     | RETURN   | 1   | 1              | |

