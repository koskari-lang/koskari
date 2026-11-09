# compile/decorate

**Syntax:** `(decorate [DECORATOR ...] FORM)`

| Form | Behaviour |
|------|-----------|
| `(decorate [] FN)` | the value of `FN`, undecorated |
| `(decorate [D ...] (def NAME FORMALS BODY ...))` | binds `NAME`, as `var` does, to the function with `[D ...]` applied |
| `(decorate [D ...] (var NAME VALUE))` | binds `NAME`, as `var` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (var NAME :: TYPE VALUE))` | binds `NAME :: TYPE`, as `var` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (global var NAME VALUE))` | binds `NAME`, as `global var` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (global var NAME :: TYPE VALUE))` | binds `NAME :: TYPE`, as `global var` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (define NAME VALUE))` | binds `NAME`, as `var` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (define-global NAME VALUE))` | binds `NAME`, as `define-global` does, to `VALUE` with `[D ...]` applied |
| `(decorate [D ...] (var NAME))`, or any other declaration | a `CompilerError`, nothing to decorate |
| `(decorate [D ...] (global def NAME FORMALS BODY ...))` | binds `NAME`, as `define-global` does, to the function with `[D ...]` applied |
| `(decorate [D ...] (global class NAME SUPERS BODY ...))` | binds `NAME`, as `define-global` does, to the class [`build-class`](../specials/compile-build-class.md) builds, with `[D ...]` applied |
| `(decorate [D ...] (define [NAME FORMALS ...] BODY ...))` | binds `NAME`, as `var` does, to the function with `[D ...]` applied |
| `(decorate [D ...] (define [(NAME :: TYPE) FORMALS ...] BODY ...))` | binds `NAME`, as `var` does, to the function with return annotation `TYPE` and `[D ...]` applied |
| `(decorate [D ...] (class NAME SUPERS BODY ...))` | binds `NAME`, as `var` does, to the class `build-class` builds, with `[D ...]` applied |
| `(decorate [D ...] (define-global [NAME FORMALS ...] BODY ...))` | binds `NAME`, as `define-global` does, to the function with `[D ...]` applied |
| `(decorate [D ...] (define-global [(NAME :: TYPE) FORMALS ...] BODY ...))` | binds `NAME`, as `define-global` does, to the function with return annotation `TYPE` and `[D ...]` applied |
| `(decorate [D REST ...] FN)` | applies `D` to the result of decorating `FN` with `[REST ...]`, behaving like `(decorate-subject D (decorate [REST ...] FN))` |

**Behaviour:** What `@` lines read to, and usable directly. The name of
a `def`, `var`, `class`, `global var`, `global def`, `global class`,
`define` or `define-global` subject is
hoisted out so the decorated value is what gets bound; then the
decorator list folds through
[`decorate-subject`](compile-decorate-subject.md) one decorator per
step, outermost first, so `@d1` over `@d2` over a definition applies as
`(d1 (d2 ...))`, as in Python.

A typed value subject keeps its annotation and wraps only the value, so
`@d var x :: int 0` in a class body still records `x` in
`__annotations__`; `type: TYPE` reads the same as `:: TYPE`. A
declaration, `var x`, `var [a b]`, `global var x`, `define x` or
`define-global x`, with or without a type, binds no value, so a
decorator over it has nothing to transform and the compiler raises
`CompilerError` naming the form. A binding form of any other shape is
not a declaration; it reaches its special and fails as it would
undecorated, with `SyntaxMatchError`.

**Examples:**

```koskari
import-from functools lru_cache

@(lru_cache maxsize: None)
@demand
def fib [n]:
  if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))
```

The layout lines read as
`(decorate ((lru_cache maxsize: None) demand) (def fib ...))`. See
[Decorators](../../guide/decorators.md).
