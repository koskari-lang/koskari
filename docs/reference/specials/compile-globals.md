# compile/globals
**Symbol:** `compile/globals`

**Syntax:** `(globals)`

**Use-case:** Obtain the module globals namespace for the current frame:
walk `KosNamespace_PARENT` from the frame's namespace to the outermost parent
(analogous to Python's `globals()`). Useful for reflection, explicit binding
into the module namespace, or passing the globals dict to APIs that expect it.

**Behaviour:** Emits `GLOBALS` (argument 0). Pushes that namespace onto the
stack as the expression's value. Any arguments are rejected with `SpecialError`.

Python's `repr` of the namespace is `<ModuleNamespace NAME: name=value ...>`,
listing the module's bindings; Koskari's `repr` prints it as a dict
literal. Function and `let` namespaces from [`(vars)`](compile-vars.md)
print as `<Namespace NAME in PARENT: ...>`.

**Examples:**

```koskari
;; Module globals namespace for the current frame
(globals)
```

**See also:** [`vars`](compile-vars.md) for the current scope's namespace.
