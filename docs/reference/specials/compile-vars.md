# compile/vars
**Symbol:** `compile/vars`

**Syntax:** `(vars)` or `(vars OBJ)`

**Use-case:** With no operand, obtain the namespace of the current scope:
the module namespace at top level, the call's namespace inside a function
or `let` body. With one operand, obtain `OBJ`'s attribute dict, as
Python's `vars(obj)` does.

**Behaviour:**

- `(vars)` emits `LOCALS` (argument 0) and pushes the current frame's
  namespace. The namespace is a mapping, so `(item (vars) "x")` reads the
  binding `x`.
- `(vars OBJ)` emits `OBJECT_VARS` and pushes `OBJ.__dict__`. An object
  without a `__dict__` raises `TypeError`.
- Two or more operands are rejected at compile time with
  `SyntaxMatchError`.

`vars` in value position, for example passed to `map`, is Python's
builtin `vars`. The runtime builtin [`koskari-vars`](../runtime/koskari-vars.md)
returns the calling Koskari frame's namespace when called with no
argument, including through `apply` or from a callback.

**Namespace repr:** a function or `let` namespace prints as its type
name, its own name, ` in ` and the parent namespace's name, then
`name=value` for each name the scope declares. A declared name with no
value yet shows `name=<unbound>`, and a namespace that holds itself shows
`<Namespace ...>` at the inner occurrence. An empty scope omits the colon.
A module namespace prints as its type, its name and its bindings as a
mapping, each in the language of the caller: Python's `repr` gives
`<ModuleNamespace 'NAME' {'k': 3}>` and Koskari's gives
`<ModuleNamespace "NAME" #{"k": 3}>`. A binding that holds the namespace
itself shows `<ModuleNamespace ...>` at the inner occurrence.

```koskari
>>> (def f1 [a] (var scale 2) (vars))
>>> (f1 1)
<Namespace f1 in __main__: a=1 scale=2>
>>> (def g [] (vars))
>>> (g)
<Namespace g in __main__>
>>> (def h [] (var r (repr (vars))) (var after 1) r)
>>> (h)
"<Namespace h in __main__: r=<unbound> after=<unbound>>"
>>> (def u [] (var self-ref (vars)) self-ref)
>>> (u)
<Namespace u in __main__: self-ref=<Namespace ...>>
```

**Examples:**

```koskari
;; the binding x of the enclosing let
>>> (let [[x 5]] (item (vars) "x"))
5

;; an instance's attribute dict
>>> (class Point [] (def __init__ [self x] (set-attr self x x)))
>>> (vars (Point 3))
{'x': 3}
```

**See also:** [`globals`](compile-globals.md) for the module namespace
from any scope, [`koskari-vars`](../runtime/koskari-vars.md).

**Also:** function [`vars`](../runtime/functions.md#attributes-and-items).
