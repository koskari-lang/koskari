# compile/class

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/class`

**Syntax:** `(compile/class NAME (SUPERS) BODY ...)`

- **NAME**: symbol — used for the Python class `__name__` (and the BUILD_CLASS
  position tuple); **not** a compile-time binding target for this special
- **SUPERS**: list of superclass expressions and optional `meta: Metaclass` keyword
- **BODY**: forms compiled in sequence inside the CollectorNamespace (e.g.
  `compile/define` for class attributes and methods)

**Use-case:** Create a Koskari class via PUSH_COLLECT/POP_COLLECT/BUILD_CLASS and
run the body in a CollectorNamespace. The **value of the form** is the new class
object (default metaclass is KosType; use `meta: MyMeta` in SUPERS for a custom
metaclass). To **bind** the class to a name in the surrounding scope, wrap with
`compile/define` (or use the **`define-class`** macro — see
[MACROS.md](../../design/MACROS.md) § compile/define-class).

**Behaviour:** Builds a position tuple `(name, bases)` or
`(name, bases, metaclass)` with BUILD_ARGS, opens a collector namespace
(PUSH_COLLECT / POP_COLLECT around the body), compiles **BODY** with
`helper_begin`, then emits **BUILD_CLASS**. Unless the body already declares
them, the compiler stores **`__qualname__`** as **NAME** (CPython-style,
scope-only; nested `Outer.Inner` would need a future nesting stack) and
**`__module__`** from the compiling **`Module.__name__`**, falling back to
**`compiler.name`**, and skips **`__module__`** injection when no non-empty
string is found. A
leading string in **BODY** is stripped as the class
`__doc__` when `__doc__` is not already declared (same pattern as functions).
The collect scope uses a stack block so body expression results (e.g. `None`
from `compile/define`) do not reach **BUILD_CLASS**. The class object is left on
the stack as the expression result. This special does **not** emit `declare_var`
or `VAR_STORE` for **NAME** itself (only for injected `__doc__` /
`__qualname__` / `__module__` when applicable).

**Examples:**

```scheme
;; Expression value is the class (e.g. bind with compile/define)
(compile/define C (compile/class C ()))

;; Class with attribute
(compile/define C
  (compile/class C ()
    (compile/define x 42)))

;; Class with superclass (object is available from bootstrap)
(compile/define C
  (compile/class C (object)
    (compile/define x 1)))

;; Custom metaclass
(compile/define C
  (compile/class C (Base meta: MyMeta)
    (compile/define x 1)))
```

**Tests:** `tests/compile/specials/test_class.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
