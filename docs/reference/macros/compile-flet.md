# compile/flet
**Syntax:** **`(flet [[NAME FORMALS BODY …] …] FORM …)`**

**Behaviour:** Binds local functions for the extent of a body. Each declaration
becomes a **`let`** binding whose value is an anonymous **`lambda`**:

```koskari
(flet [[double [x] (* x 2)]
       [inc [x] (+ x 1)]]
  (double (inc 20)))
```

is equivalent to:
```koskari
(let [[double (lambda [x] (* x 2))]
      [inc (lambda [x] (+ x 1))]]
  (double (inc 20)))
```

Because **`let`** initializers are evaluated in the enclosing scope, a binding
shadows an outer function of the same name but does **not** see its siblings in
the same **`flet`**, so the bound functions cannot be mutually recursive. Reach
for [`labels`](compile-labels.md) when the functions must call one another.
