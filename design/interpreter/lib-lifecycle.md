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
- **Shared, not owned.** A later init, from a re-import or a legacy
  subinterpreter, finds the statics in place and fills its own module
  dict from them.
- **No module slot tears down.** The module definition leaves `m_clear`
  and `m_free` NULL. The static types and their instances hold no
  reference to the module object, so they outlive it: the module dies in
  the collection that frees the builtins namespace holding its
  functions, and the interpreter's last collection then runs finalizers
  that may still intern a symbol or render a pair. A clear from either
  slot left those readers on NULLed statics. The statics live for the
  process, as the static types do.
- **A host of its own.** `koskari_lib_clear` empties every unit's
  statics and the module dict for a host that fills a module it made
  itself and tears it down directly, as the native test harness does
  before finalizing. A second clear before the next init is a no-op.
  `tests/native/test_lifecycle.c` runs init, clear, init over one module
  and interns a symbol after the second init.

Isolated subinterpreters, which refuse single-phase modules, are not a
target. The multi-phase form with per-module state would make every
static a struct field and every static type a heap type for a property
the runtime does not need.
