# compile/decorate

**Syntax:** `(decorate [DECORATOR ...] FORM)`

| Form | Expands to |
|------|------------|
| `(decorate [] FN)` | `FN` |
| `(decorate [D ...] (def NAME FORMALS BODY ...))` | `(var NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (var NAME VALUE))` | `(var NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (var NAME :: TYPE VALUE))` | `(var NAME :: TYPE (decorate [D ...] VALUE))` |
| `(decorate [D ...] (global var NAME VALUE))` | `(global var NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (global var NAME :: TYPE VALUE))` | `(global var NAME :: TYPE (decorate [D ...] VALUE))` |
| `(decorate [D ...] (define NAME VALUE))` | `(var NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (define-global NAME VALUE))` | `(define-global NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (var NAME))`, or any other declaration | a `CompilerError`, nothing to decorate |
| `(decorate [D ...] (global def NAME FORMALS BODY ...))` | `(define-global NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (global class NAME SUPERS BODY ...))` | `(define-global NAME (decorate [D ...] (build-class NAME SUPERS BODY ...)))` |
| `(decorate [D ...] (define [NAME FORMALS ...] BODY ...))` | `(var NAME (decorate [D ...] (function NAME [FORMALS ...] BODY ...)))` |
| `(decorate [D ...] (define [(NAME :: TYPE) FORMALS ...] BODY ...))` | `(var NAME (decorate [D ...] (function NAME :: TYPE [FORMALS ...] BODY ...)))` |
| `(decorate [D ...] (class NAME SUPERS BODY ...))` | `(var NAME (decorate [D ...] (build-class NAME SUPERS BODY ...)))` |
| `(decorate [D ...] (define-global [NAME FORMALS ...] BODY ...))` | `(define-global NAME (decorate [D ...] (function NAME [FORMALS ...] BODY ...)))` |
| `(decorate [D ...] (define-global [(NAME :: TYPE) FORMALS ...] BODY ...))` | `(define-global NAME (decorate [D ...] (function NAME :: TYPE [FORMALS ...] BODY ...)))` |
| `(decorate [D REST ...] FN)` | `(decorate-subject D (decorate [REST ...] FN))` |

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
`CompilerError` naming the form.

**Examples:**

```koskari
>>> (macroexpand-1 '(decorate (d1) (def foo [x] x)))
(var foo (decorate [d1] (function foo [x] x)))
```

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
