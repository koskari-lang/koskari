# compile/def

**Syntax:** `(def NAME FORMALS BODY ...)` or
`(def NAME :: TYPE FORMALS BODY ...)`

**Behaviour:** Expands to `(var NAME (function NAME FORMALS BODY ...))`,
defining a function and binding it to **NAME** in the scope the form
appears in, as [`var`](../specials/compile-var.md) binds. **FORMALS**
take every shape [`lambda`](../specials/compile-lambda.md) accepts. A
`:: TYPE` after the name passes through to
[`function`](../specials/compile-function.md) as the return annotation.
To bind the function at the module from inside a body, write
`global def`; see [compile/global](../specials/compile-global.md).

`def` takes a name, formals, and a body, never a plain value:
`(def x 1)` expands to `(var x (function x 1))`, which fails to compile
because `1` is not a formals list. A value binding is `var`.

A decorator line above `def` decorates the function and binds the
result; see [`decorate`](compile-decorate.md).

**Examples:**

```koskari
def double [x]:
  * x 2

(double 21)    ;; 42

def half :: float [x]:
  / x 2
```

The s-expression form:

```koskari
(def fact [n]
  (if (<= n 1) 1 (* n (fact (- n 1)))))
```

Decorated:

```koskari
def plus-one [f]:
  lambda [x]:
    + (f x) 1

@plus-one
def square [x]:
  * x x

square 4     ;; 17
```

**See also:** [`var`](../specials/compile-var.md),
[`function`](../specials/compile-function.md),
[Decorators](../../guide/decorators.md).
