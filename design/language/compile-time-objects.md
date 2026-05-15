# Compile-Time Object Hierarchy

**Status:** Planned
**Author:** Christopher O'Brien <obriencj@preoccupied.net>

---

## Background

Koskari's compiler dispatches on the head of a cons at compile time. When
`compile_pair` encounters `(foo ...)`, it calls `find_compiled(foo)` to see
if `foo` names a compile-time object. If found, the object's expansion logic
runs instead of emitting a normal function call.

In the current implementation `find_compiled` checks for a `compile_impl`
attribute (set by the `@special` decorator). This is a duck-typing hack — it
works, but it makes every compile-time object indistinguishable from any other,
and makes the `@special` decorator the only way to register them. A proper
class hierarchy gives each kind a distinct type, a clean interface, and a
natural place for docstrings without pydoc workarounds.

Python-sibilant went through the same evolution. Its `Compiled` /
`Special` / `Macro` / `Alias` / `Operator` hierarchy in
`sibilant/compiler/__init__.py` is the direct precedent for what is
described here.

---

## The `compile/` Namespace Convention

All compile-time objects live in the module namespace under a `compile/`
prefix. `compile/if`, `compile/begin`, `compile/quasiquote` are specials;
`compile/my-if` would be a user-defined macro.

At call sites, users write the bare name: `(my-if cond then else)`. The
compiler's `find_compiled` adds the prefix automatically. This means:

- Source reads naturally without `compile/` noise.
- The `compile/` namespace is explicit and greppable for tooling.
- A single name can have both a compile-time form (`compile/foo`) and a
  runtime form (`foo`), enabling first-class use.

### Two-step lookup

`find_compiled` should search in two steps:

1. Try the bare name as-is — if it is a `Compiled` instance, return it.
   This covers the rare case where a compile-time object is registered
   without the prefix (e.g. for bootstrapping or aliasing).
2. Try `compile/<name>` — if found and a `Compiled` instance, return it.
3. Neither matched → `None`; fall through to normal call compilation.

The current single-step implementation always prepends `compile/`,  which
means step 1 is missing. Adding it is a small, non-breaking change.

---

## Class Hierarchy

```
Compiled  (abstract base)
├── Special
├── Macro
│   └── Alias
└── Operator   (future)
```

### `Compiled` — abstract base

```python
class Compiled:
    """
    Abstract base for all compile-time objects.

    Every Compiled has a name (used in repr and error messages) and an
    expand(compiler, source, tc) method that the compiler calls when the
    object is found in head position of a cons.
    """

    def __init__(self, name):
        self.__name__ = name

    def __call__(self, *args, **kwargs):
        raise TypeError("compile-time object %r cannot be called at runtime" % self)

    def __repr__(self):
        return "<%s %r>" % (self.__objname__, self.__name__)

    def expand(self, compiler, source, tc=False):
        raise NotImplementedError
```

`_is_compiled(v)` becomes `isinstance(v, Compiled)`. The existing
duck-typing check (`hasattr(v, 'compile_impl')`) is kept as a fallback
during migration and removed once all specials are migrated.

### `Special` — built-in compiler special forms

Wraps a Python function `(compiler, source, tc) -> ByteCodeFragment | cons`.
The function is stored as a plain attribute (not a bound method) so that
`Special.__new__` can set `__doc__` from the function's docstring.

**The pydoc problem**: python-sibilant used `type(name, (Special,), {...})`
to create a *new subclass per instance* so that `help(special_instance)`
showed the function's docstring rather than `Special.__doc__`. This is a
hack driven by pydoc looking at the class `__doc__`, not the instance
`__doc__`. Koskari should avoid per-instance subclassing; instead, override
`__doc__` as an instance attribute and patch any tooling that needs it.
Instances are the right granularity, not subclasses.

```python
class Special(Compiled):
    __objname__ = 'special form'

    def __init__(self, name, fn):
        super().__init__(name)
        self.__doc__ = fn.__doc__
        self._fn = fn

    def expand(self, compiler, source, tc=False):
        return self._fn(compiler, source, tc)
```

The `@special` decorator is updated to produce a `Special` instance:

```python
def special(form_symbol):
    def decorator(fn):
        inst = Special(str(form_symbol), fn)
        SPECIALS[form_symbol] = inst
        return inst
    return decorator
```

### `Macro` — user-defined compile-time transforms

A `Macro` wraps a callable (the compiled macro body) that accepts the
unevaluated argument forms and returns a new source cons. The compiler
then re-compiles the result — this is the two-phase compile model already
implemented for `compile/quasiquote-block`.

```python
class Macro(Compiled):
    """
    A Macro transforms source at compile time.

    The wrapped function receives the unevaluated argument list and returns
    a new source cons, which the compiler then dispatches normally.
    It is an error to invoke a Macro at runtime.
    """

    __objname__ = 'macro'

    def __init__(self, name, fn):
        super().__init__(name)
        self.__doc__ = fn.__doc__
        self._fn = fn

    def expand(self, compiler, source, tc=False):
        called_by, args = source
        result = self._fn(*args.unpack())
        return result
```

`defmacro name [formals]: body` compiles to roughly:

```python
compile_my_macro = Macro('my-macro', compiled_body_fn)
ns.store('compile/my-macro', compile_my_macro)
```

When the compiler encounters `(my-macro arg1 arg2)`, `find_compiled`
returns the `Macro` instance, `expand` calls `compiled_body_fn(arg1,
arg2)`, and the returned cons is re-compiled.

### `Alias` — symbol-level substitution

An `Alias` wraps a zero-argument callable that returns a symbol or other
constant. When the compiler encounters the alias name in any position
(symbol or head of cons), it substitutes the result. Useful for
compile-time renaming and import aliasing.

```python
class Alias(Macro):
    """
    An Alias substitutes a symbol or constant at compile time.

    The wrapped function takes no arguments and returns the substitution.
    In head position, the result replaces the head and the cons is
    recompiled. In symbol position, the result replaces the symbol.
    """

    __objname__ = 'alias'

    def expand(self, compiler, source, tc=False):
        substitution = self._fn()
        if is_pair(source):
            called_by, rest = source
            result = cons(substitution, rest)
            set_position(result, get_position(source))
            return result
        return substitution
```

### `Operator` — dual compile-time and runtime form (future)

An `Operator` has both a `compile_impl` (like `Special`) and a
`runtime_impl` (a callable). When the compiler encounters it in head
position it uses `compile_impl`; when the name is looked up as a value
(not in head position), it returns `runtime_impl`. This supports the
dual-existence pattern: `my-if` as a runtime callable for higher-order
use, `compile/my-if` as the macro for call-site expansion.

Not planned for immediate implementation; documented here for reference.

---

## `find_compiled` — revised

```python
def find_compiled(self, name):
    bare = str(name)
    prefixed = 'compile/' + bare if not bare.startswith('compile/') else bare

    ns = vars(self.module)
    for key in (bare, prefixed):
        try:
            r = ns.ref(key)
        except (KeyError, AttributeError, TypeError):
            continue
        if r is None or r.empty:
            continue
        v = r.deref()
        if isinstance(v, Compiled) or _is_compiled(v):
            return v

    return None
```

The `_is_compiled` duck-typing fallback remains during migration; once
all specials are `Special` instances it is removed.

---

## `defmacro` — planned special form

`defmacro` is a compile-time special that:

1. Compiles the macro body (a `lambda`-like form accepting the unevaluated
   arguments).
2. Runs that compiled body to produce a Python callable.
3. Wraps it in a `Macro` instance.
4. Stores the `Macro` under `compile/<name>` in the current module
   namespace.

```
defmacro my-if [cond then else]:
  `begin:
    if ,cond:
      ,then
    else:
      ,else
```

Compiles to code that, when run, registers `compile/my-if` as a `Macro`
whose `expand` function calls the quasiquote-block body with `cond`,
`then`, `else` bound to the unevaluated arguments from the call site.

The macro expansion pipeline (`compile/quasiquote-block` → `compile/quasiquote`
→ quasi-evaluated source that may still contain `:block:` shells →
`apply_layout` in finalize → hygiene / Datum peel) is how block-syntax macro
bodies yield layout-expanded source before ordinary compilation continues.

---

## Migration Path

1. **Add `Compiled`, `Special`, `Macro`, `Alias`** to
   `koskari/compile/compiler.py` (or a new `koskari/compile/objects.py`).
2. **Update `@special` decorator** in `koskari/compile/helpers.py` to
   produce `Special` instances and store them in `SPECIALS`.
3. **Update `_is_compiled`** to `isinstance(v, Compiled)`, keeping the
   duck-typing fallback.
4. **Update `find_compiled`** to two-step lookup and accept both
   `Compiled` instances and duck-typed objects.
5. **Update `compile_special`** to call `v.expand(compiler, source, tc)`
   instead of `v.compile_impl(compiler, source, tc)`, with a fallback to
   `compile_impl` during migration.
6. **Add `defmacro` special** once the above is stable.
7. **Remove duck-typing fallback** once all registered objects are
   `Compiled` instances.

---

## Key Differences from python-sibilant

- **No per-instance subclassing**: python-sibilant creates a new `type`
  per `Special`/`Macro` instance to work around pydoc. Koskari avoids
  this; instances are instances.
- **`compile/` prefix is the namespace**, not embedded in class identity.
  python-sibilant registers under the bare name; koskari uses the prefix
  to separate compile-time from runtime in a single shared namespace.
- **`expand` vs `compile`**: python-sibilant uses a `compile(compiler,
  source, tc, cont)` interface with a trampoline continuation. Koskari's
  simpler `expand(compiler, source, tc)` returns either a
  `ByteCodeFragment` (final) or a cons (to be re-compiled), matching the
  existing `full_compile` loop.
