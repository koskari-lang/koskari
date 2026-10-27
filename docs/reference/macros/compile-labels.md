# compile/labels
**Syntax:** **`(labels [[NAME FORMALS BODY ...] ...] FORM ...)`**

**Behaviour:** Like [`flet`](compile-flet.md), but the bound names are visible to
one another, so the functions can be mutually recursive. Each declaration is
bound with **`var`** inside a shared (empty) **`let`** scope, so a sibling
reference resolves there before the body runs:

```koskari
(labels [[even? [n] (if (== n 0) True (odd? (- n 1)))]
         [odd? [n] (if (== n 0) False (even? (- n 1)))]]
  (even? 10))
```

is equivalent to:
```koskari
(let []
  (var even? (function even? [n] (if (== n 0) True (odd? (- n 1)))))
  (var odd? (function odd? [n] (if (== n 0) False (even? (- n 1)))))
  (even? 10))
```

A binding may call any sibling, including one declared later in the same
**`labels`** form.

As with any local binding, a **`labels`** binding whose name is also a **macro**
(a special form, or one of the **`cad*r`** accessor macros) does not take effect
in call position -- macros expand before names resolve, so they always win. Names
bound to runtime functions are shadowed normally.
