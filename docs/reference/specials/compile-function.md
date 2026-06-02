# compile/function
**Symbol:** `compile/function`

**Syntax** (named only; most specific forms first):


| Shape                    | Form                                           |
| ------------------------ | ---------------------------------------------- |
| Named + return           | `(function NAME :: RET FORMALS BODY ...)`     |
| Named + return (`type:`) | `(function NAME type: RET FORMALS BODY ...)`  |
| Named, no return         | `(function NAME FORMALS BODY ...)`            |


There is no anonymous `function` form; use [lambda](compile-lambda.md) for that.

**Formals, return type, and docstring:** Same rules as [compile/lambda](compile-lambda.md).
`RET` is recorded on `Code` as `code.extras['return']` and exposed as
`__annotations__['return']`. A leading string in `BODY` becomes `__doc__`.

**Use-case:** Create a **named** function value whose body must **not** receive
a closure self-binding for `NAME` at `BUILD_FUNCTION` time. The name in the
body resolves via outer or module bindings instead — e.g. after decorators wrap
the stored definition, or when [define](compile-define.md) /
[define-global](compile-define-global.md) function shorthand stores the
function and recursive calls use that binding.

**Behaviour:** Same pipeline as `compile/lambda` via `helper_function` with
`selfref=False`: `gather_formals`, `helper_begin`, `FunctionSpec`,
`BUILD_FUNCTION`. Does **not** bind `NAME` in the inner function scope at
creation (contrast named `lambda`).

**See also:** [compile/lambda](compile-lambda.md) — anonymous and named forms
with optional inner self-reference for named `lambda`.

**Examples:**

```koskari
;; Named function (no inner self-ref for double)
(function double [x] (mul x 2))

;; Return type
(function f :: int [x] x)

;; Docstring + return type
(function g :: int [x] "doc for g" x)
```
