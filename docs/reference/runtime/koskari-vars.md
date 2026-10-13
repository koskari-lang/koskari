# koskari-vars

Runtime builtin (not a macro), native to the runtime.

**Syntax:** `(koskari-vars)` or `(koskari-vars OBJ)`

**Behaviour:**

- With no argument, returns the namespace of the Koskari frame that made
  the call. A call in tail position still returns the calling function's
  namespace. Through `apply`, or as a callback run by a higher-order
  function such as `map`, it returns the namespace of the Koskari function
  that invoked it.
- With an argument, returns `OBJ.__dict__`, and raises `TypeError` when
  `OBJ` has none.
- Called from Python with no argument and no Koskari evaluation running,
  it raises `RuntimeError`.

Python's builtin `vars`, which is what the name `vars` holds in value
position, reads the CPython frame. `koskari-vars` never does; it reads the
running Koskari evaluation. In call position `(vars)` is the
[`compile/vars`](../specials/compile-vars.md) special, which compiles to
the current namespace directly; use `koskari-vars` where a function value
is needed.

The returned namespace prints as described on the
[`compile/vars`](../specials/compile-vars.md) page.

**Examples:**

```koskari
>>> (def g [n] (var twice (* n 2)) (koskari-vars))
>>> (g 4)
<Namespace g in __main__: n=4 twice=8>
>>> (def h [n] (apply koskari-vars #[]))
>>> (item (h 3) "n")
3
>>> (list (map (lambda [x] (item (koskari-vars) "x")) #[1 2 3]))
[1, 2, 3]
```
