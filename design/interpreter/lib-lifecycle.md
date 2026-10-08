# The `koskari.lib` module lifecycle

`koskari.lib` is a single-phase extension module. Its state is not
module state but process statics: interned string constants, the symbol
and keyword tables, freelists, and the static type objects, readied once
and added to the module dict. The contract around them:

- **Init is re-runnable.** `koskari_lib_init` runs every unit's init over
  a module object. Each init creates a static only where a clear left it
  empty, and `PyType_Ready` is a no-op the second time, so a second init
  rebuilds what a clear emptied and leaves the rest alone. The module
  definition says so with `m_size` 0, which tells CPython that a
  re-import after the module object was collected runs `PyInit_lib`
  again instead of restoring a saved dict over emptied statics.
- **One owner.** The first module `PyInit_lib` makes owns the statics. A
  later init, from a re-import or a legacy subinterpreter, shares them
  without taking them over.
- **Teardown is the owner's.** `koskari_lib_clear` empties every unit's
  statics and the module dict. It runs through `m_clear` when the
  collector breaks the cycle between the module and its functions, at
  `Py_Finalize` for one, and through `m_free` when the module object is
  deallocated outside a cycle. For any module but the owner it is a
  no-op.
- **A host of its own.** The native test harness fills a module it made
  itself and clears it directly; no owner is named there, so the clear
  proceeds. `tests/native/test_lifecycle.c` runs init, clear, init over
  one module and interns a symbol after the second init.

Isolated subinterpreters, which refuse single-phase modules, are not a
target. The multi-phase form with per-module state would make every
static a struct field and every static type a heap type for a property
the runtime does not need.
