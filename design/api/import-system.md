# Koskari Import System

The import system allows Python and Koskari code to import `.ki` Koskari source
files transparently via Python's standard `import` machinery.

## Overview

- **Finder**: `install()` registers one path hook on `sys.path_hooks`, just
  ahead of Python's own FileFinder hook. The FileFinder it builds for a
  directory carries Python's loaders (extension, `.py`, `.pyc`) and then
  `KoskariSourceFileLoader` for `.kio` and `.ki`, so a directory is served by
  one finder and a module name resolves by Python's precedence: builtins and
  frozen modules first, `sys.path` order between directories, Python's
  suffixes before Koskari's within one. Nothing of Koskari's sits on
  `sys.meta_path`.
- **Loader**: `KoskariSourceFileLoader` drives the read-compile-execute loop for
  `.ki` files. The synchronous `exec_module` entry point uses `exec_module_demand`
  (demand path). An async `exec_module_async` path exists for callers that run an
  event loop.
- **Compiler specials**: The `import`, `import-from`, `import-all-from`,
  and `use` special forms emit the self-contained `IMPORT`, `IMPORT_FROM`,
  `IMPORT_STAR`, and `IMPORT_USE` opcodes (`use` with no members emits
  `IMPORT_STAR`); see [SPECIALS.md](../SPECIALS.md).

## Installation

- **install()**: Register the directory hook on `sys.path_hooks` and clear
  `sys.path_importer_cache` so directories re-resolve through it. Idempotent.
- **uninstall()**: Remove the hook and clear `sys.path_importer_cache`.
- **Auto-install**: `koskari/__init__.py` calls `install()` at package import
  time. Suppressed when the `KOSKARI_NOIMPORTER` environment variable or the
  `-X KOSKARI_NOIMPORTER` xoption is set to a non-zero value.

## Module Lifecycle

**Async path**: `new_module` → `load_module_async` (each expression:
coroutine + await).

**Demand path**: `new_module` → `load_module_demand` (each expression:
`demand_call(expr)`).

Both paths start from the same module: `new_module` copies the public
bindings of the loaded `koskari.builtins` module, which `koskari.bootstrap`
assembles from the Python registry plus basics.ki.

1. **new_module(name, package)**: Create a koskari `Module` with a fresh
   `ModuleNamespace` whose parent bindings are copied from `koskari.builtins`.
2. **init_module(module, filename, spec)**: Set `__file__`, `__path__` for
   packages, and `__spec__`/`__loader__` when provided.
3. **load_module_async(module)** / **load_module_demand(module)**: Read each
   top-level expression from the source file, compile it, and run it (async:
   coroutine + await; demand: `demand_call(expr)`). Bindings from one
   expression are visible to subsequent ones.
4. **materialize_exports(module)**: After the last expression and before the
   load facets detach, `__all__` is materialized from the feature stems that
   `declare-exports` recorded in `__features__`: each stem expands to the
   binding keys that actually exist -- the runtime name and its `compile/`
   and `layout/` facets -- so `__all__` stays plain Python (every entry a
   real attribute) while carrying transformer facets for `IMPORT_STAR` and
   the bare `use` form. A module with no `__features__` is left alone. Every
   loader runs this stage: both `load_module_*` paths,
   `load_module_source_async`, and the serializer's compile loop in
   `koskari.serialize.incremental`.

## Implementation

| Component | Module | Notes |
|-----------|--------|-------|
| install / uninstall | `koskari.importer` | `install()`, `uninstall()`, `is_installed()` |
| Directory hook | `koskari.importer` | a `FileFinder.path_hook` on `sys.path_hooks` carrying Python's loaders and the Koskari one |
| Loader | `koskari.importer` | `KoskariSourceFileLoader` |
| Module lifecycle | `koskari.module` | `new_module`, `init_module`, `load_module_async`, `load_module_demand`, `materialize_exports` |
| Builtins | `koskari.builtins` | the assembled builtins module `new_module` copies from; built by `koskari.bootstrap` (`make_bootstrap()` plus the `.ki` sources) |

## Source Suffixes

Both `.ki` and `.kio` are registered. A `.kio` with no sibling source stands.
Beside a source it stands only when its stamp matches that source's current
mtime and size; an unstamped, unreadable, or wrong-ABI `.kio` yields to the
`.ki` (see `source_for_stale_kio`).
