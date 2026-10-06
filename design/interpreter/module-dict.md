# A Koskari module's `__dict__`

Status: resolved. Recorded 2026-10-01; the dict contract was met when
`ModuleNamespace` became a dict subclass with the mapping methods
answering tier 3 and a dict body holding the mirrored dunders, and the
module type became a `types.ModuleType` on 2026-10-06 (below).

## The constraint

A Koskari module must be a proper Python module. The stdlib reaches
`sys.modules[name].__dict__` and hands it to `exec` and `eval` as
globals: `dataclasses` for the methods it generates,
`typing.get_type_hints` and `inspect.get_annotations` for string
annotations, `attrs` the same way. `exec` and `eval` accept a `dict` or
a `dict` subclass and nothing else. `doctest`, `pydoc`, `pickle` and
`copyreg` read the dict as a mapping.

Since b873fd2e the loader places the Koskari `Module` itself in
`sys.modules`. That fixed three identity defects of the earlier
copy-onto-a-Python-module scheme: a global a module function rebound
was stale when Python read it, an attribute Python assigned never
reached the module's own functions, and a circular `.ki` import found
an empty module. It also made `module.__dict__` a `ModuleNamespace`,
which is not a dict, so `@dataclass` in an imported `.ki` file fails at
import (`tox -qe churn -- field` showed it). The identity fix stays;
the dict contract has to be met on top of it.

`ModuleNamespace` is now a dict subclass: the dict body is a real
`PyDictObject` that holds only the mirrored dunders (`__name__`,
`__package__`, `__spec__`, `__file__`, `__loader__`, `__doc__`), kept
in step by `mdl_ns_mirror`, and the mapping methods answer tier 3 from
the refs. A C caller that reads the body with `PyDict_GetItem` sees the
mirrored names only; the attribute and mapping protocols see every
binding.

`Module` is a `types.ModuleType` subclass with no field of its own: the
namespace sits in the base's dict slot (`PyModule_Type.tp_dictoffset`),
so `isinstance(mod, types.ModuleType)`, `inspect.ismodule`, `doctest`,
`pydoc`, `inspect.getfile` and `PyModule_GetDict` all work, and the
base's dealloc, traverse, clear and repr serve. Attribute access stays
Koskari's getattro/setattro over the namespace; the base's `__init__`
is masked by a no-op since the namespace is the module's whole state.
An `AttributeError` names the module by name, never by repr: the
base's repr goes through importlib, which reads attributes, and a repr
inside the error text would recurse.

## Tried and set aside: an in-place snapshot dict

One plain dict per module, created on the first `__dict__` access and
rebuilt in place on every access from the module's own names and the
values their references hold, keys never dropped, writes through it
unsupported. It satisfies every stdlib reader, and the field kernels
ran. It was set aside because `vars(module)` is how the namespace is
reached everywhere: the compiler, the loader, the REPL, the bootstrap
specials written in Koskari, the test harnesses, and tools. Each of
those had to move to a second attribute carrying the namespace, the
split leaked into Koskari source, and a fallback for "any object whose
vars() is the namespace" was reached with objects that are neither.
The change made more problems than it closed.

## Two directions to investigate

### A dict subclass that reads through the namespace

A `dict` subclass whose storage is nominally its own but whose reads
forward through the namespace:

- `__getitem__`, `get`, `__contains__`, `keys`, `items`, `values`,
  iteration and `len` answer from the module's own bindings, deref'ing
  each `Ref`; a dynamic reference's handler runs on the demand path.
- Writes land in the subclass's own dict storage so `exec`'s
  `__builtins__` insertion and similar succeed, and that storage is
  purged on every subsequent read so nothing written through the dict
  is ever stored for long or read back as module state. The module's
  write path stays `setattr`.
- `exec`'d code works: `LOAD_GLOBAL` takes the generic mapping path
  when globals is not an exact dict, so the overrides are honoured.
  The caveat is C code that calls `PyDict_GetItem` on globals directly;
  it sees only the purged storage. Which stdlib paths do that, and
  whether any matter, is the first thing to measure.

`vars(module)` keeps returning one object that behaves as both the
namespace view and a dict, so nothing that reaches the namespace
through `vars` has to change.

### Dict-backed module bindings (ItemRef)

Module globals stored as raw values in a real dict that is the module's
`__dict__`, with every module-level reference an `ItemRef` over
`(dict, key)`. Full parity for free: reads, writes, identity.

The cost is the reason Koskari closes over `Ref` cells for everything:
a cell deref is a pointer load, a dict lookup is a hash and probe.
Turning every global read in every closure back into a dict lookup is
not acceptable unless it is made invisible. The acceleration to
investigate: cache the value pointer on the `ItemRef` and validate it
by the dict's version (`ma_version_tag`, deprecated from 3.12) or
invalidate it from a dict watcher (`PyDict_AddWatcher`, 3.12+), with a
per-version header in `compat_3XX.h` for the split. A module-level
ref rebinding has nothing to express it under this model, since two
names cannot share one dict slot; that limit has to be stated if this
path is taken.

## What either direction must keep

- The identity behaviour from b873fd2e.
- Only the module's own names appear; builtins and inherited bindings
  do not.
- A dynamic reference resolves on the demand path, never by a bare or
  async call.
- Nothing written through the dict becomes module state.
