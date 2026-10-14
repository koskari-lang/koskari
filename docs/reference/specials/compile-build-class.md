# compile/build-class
**Symbol:** `compile/build-class`

**Syntax:** `(build-class NAME (SUPERS) BODY ...)`

- **NAME**: symbol -- used for the Python class `__name__`; **not** a
  binding target. The form binds nothing.
- **SUPERS**: list of superclass expressions and an optional
  `meta: Metaclass` keyword. No other keyword is accepted, and a `*args`
  splice is rejected.
- **BODY**: forms compiled in sequence in the class namespace (e.g.
  [`var`](compile-var.md) for class attributes and
  [`def`](../macros/compile-def.md) for methods). A method closes over
  the variables of the function the class is built in, never over the
  body's own names: a name the body binds is a class attribute, and a
  method that mentions it reads the enclosing scope's binding of that
  name (or fails with NameError), as in Python.

**Use-case:** Build a class as a value. The **value of the form** is the
new class object (default metaclass is plain `type`; use `meta: MyMeta`
in SUPERS for a custom metaclass). To bind the class to its name, use
the [`class`](../macros/compile-class.md) macro, which expands to
`(var NAME (build-class NAME SUPERS BODY ...))`; write `global class` to
bind it at the module (see [compile/global](../macros/compile-global.md)), or
store the value under another name with [`var`](compile-var.md).

**Behaviour:** The body runs in a fresh class namespace and the class is
built from it by the metaclass. The values of body expressions are
discarded; only the names the body binds reach the class. Unless the
body already sets them:

- `__qualname__` is **NAME**. It is not prefixed with an enclosing class
  or function, so a class built inside `Outer` or inside a function body
  has the bare name as its qualname.
- `__module__` is the compiling module's name. When no module name is
  known, `__module__` is left for the metaclass to fill.
- A leading string in **BODY** becomes the class `__doc__`, as for
  functions.

**NAME** is not declared or stored in the surrounding scope; after
`(build-class Box [])`, `Box` is still unbound.

**Layout:** [layout/build-class](../layouts/layout-build-class.md).

**Examples:**

```koskari
;; Expression value is the class; bind it with var
(var C (build-class C ()))

;; Class with a docstring and an attribute
(var C
  (build-class C ()
    "A C."
    (var x 42)))

;; Class with superclass
(var C
  (build-class C (object)
    (var x 1)))

;; Custom metaclass
(var C
  (build-class C (Base meta: MyMeta)
    (var x 1)))

;; The binding name and the class name may differ
(var B (build-class Box [] (var size 3)))
B.__name__      ;; "Box"
```

**See also:** [`class`](../macros/compile-class.md),
[layout/class](../layouts/layout-class.md),
[`decorate`](../macros/compile-decorate.md).
