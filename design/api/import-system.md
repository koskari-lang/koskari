# Koskari Import System

The import system allows Python and Koskari code to import `.ki` Koskari source
files transparently via Python's standard `import` machinery.

## Overview

- **Finder**: `KoskariPathFinder` is inserted at the front of `sys.meta_path` so
  `.ki` files are discovered before Python's PathFinder can claim a directory as
  a namespace package.
- **Loader**: `KoskariSourceFileLoader` drives the read-compile-execute loop for
  `.ki` files. The synchronous `exec_module` entry point uses `exec_module_demand`
  (demand path). An async `exec_module_async` path exists for callers that run an
  event loop.
- **Compiler specials**: The `import` and `import-from` special forms emit
  `IMPORT`, `IMPORT_REL`, and `IMPORT_FROM` opcodes; see [SPECIALS.md](../SPECIALS.md).

## Installation

- **install()**: Insert `KoskariPathFinder` at the front of `sys.meta_path`.
  Idempotent.
- **uninstall()**: Remove `KoskariPathFinder` from `sys.meta_path`.
- **Auto-install**: `koskari/__init__.py` calls `_auto_enable_importer()` at
  package import time. Suppressed when `KOSKARI_NOIMPORTER` is set.

## Module Lifecycle

**Async path**: `new_module` → `make_builtins()` → `load_module_async` (each
expression: coroutine + await).

**Demand path**: `new_module_demand` → `make_builtins_demand()` →
`load_module_demand` (each expression: `demand_sync(expr)`).

1. **new_module(name, package)** / **new_module_demand(name, package)**:
   Create a koskari `Module` with a fresh `ModuleNamespace` and builtins
   from `make_builtins()` or `make_builtins_demand()`.
2. **init_module(module, filename, spec)**: Set `__file__`, `__path__` for
   packages, and `__spec__`/`__loader__` when provided.
3. **load_module_async(module)** / **load_module_demand(module)**: Read each
   top-level expression from the source file, compile it, and run it (async:
   coroutine + await; demand: `demand_sync(expr)`). Bindings from one
   expression are visible to subsequent ones.

## Implementation

| Component | Module | Notes |
|-----------|--------|-------|
| install / uninstall | `koskari.importer` | `install()`, `uninstall()`, `is_installed()` |
| PathFinder | `koskari.importer.importlib` | `KoskariPathFinder` (class in `sys.meta_path`) |
| Loader | `koskari.importer.importlib` | `KoskariSourceFileLoader`, `KoskariFileFinder` |
| Module lifecycle | `koskari.importer.module` | `new_module`, `init_module`, `load_module_async`, `new_module_demand`, `load_module_demand` |
| Builtins | `koskari.importer.bootstrap` | `make_builtins()`, `make_builtins_demand()` |

## Source Suffixes

Only `.ki` is registered. Other suffixes (e.g. `.kio` for compiled cache) are
not yet integrated.
