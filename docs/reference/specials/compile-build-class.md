# compile/build-class
**Symbol:** `compile/build-class`

**Syntax:** `(build-class NAME (SUPERS) BODY ...)`

- **NAME**: symbol -- used for the Python class `__name__`; **not** a
  binding target. The form binds nothing.
- **SUPERS**: list of superclass expressions and optional
  `meta: Metaclass` keyword
- **BODY**: forms compiled in sequence in the class namespace (e.g.
  [`var`](compile-var.md) for class attributes and
  [`def`](../macros/compile-def.md) for methods). A method closes over
  the variables of the function the class is built in, never over the
  body's own names: a name the body binds is a class attribute, and a
  method that mentions it reads the enclosing scope's binding of that
  name (or fails with NameError), as in Python.

**Use-case:** Build a class as a value. `build-class` is the same
special as [compile/class](compile-class.md) under a name that says what
it does: the **value of the form** is the new class object (default
metaclass is plain `type`; use `meta: MyMeta` in SUPERS for a custom
metaclass). To bind the class, store the value with
[`var`](compile-var.md), or use `global class` to bind it at the module
(see [compile/global](compile-global.md)).

**Behaviour:** As [compile/class](compile-class.md). The body runs in a
fresh class namespace and the class is built from it. Unless the body
already sets them, `__qualname__` is **NAME** and `__module__` is the
compiling module's name. A leading string in **BODY** becomes the class
`__doc__`. **NAME** is not declared or stored in the surrounding scope;
after `(build-class Box [])`, `Box` is still unbound.

**Layout:** [layout/build-class](../layouts/layout-build-class.md).

**Examples:**

```koskari
;; Expression value is the class; bind it with var
(var C (build-class C ()))

;; Class with attribute
(var C
  (build-class C ()
    (var x 42)))

;; Class with superclass
(var C
  (build-class C (object)
    (var x 1)))

;; Custom metaclass
(var C
  (build-class C (Base meta: MyMeta)
    (var x 1)))
```
