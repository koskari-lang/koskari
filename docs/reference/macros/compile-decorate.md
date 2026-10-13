# compile/decorate

**Syntax:** `(decorate [DECORATOR ...] FORM)`

| Form | Expands to |
|------|------------|
| `(decorate [] FN)` | `FN` |
| `(decorate [D ...] (def NAME FORMALS BODY ...))` | `(var NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (var NAME VALUE))` | `(var NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (global def NAME FORMALS BODY ...))` | `(global NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (global class NAME SUPERS BODY ...))` | `(global NAME (decorate [D ...] (build-class NAME SUPERS BODY ...)))` |
| `(decorate [D ...] (define [NAME FORMALS ...] BODY ...))` | `(var NAME (decorate [D ...] (function NAME [FORMALS ...] BODY ...)))` |
| `(decorate [D ...] (class NAME SUPERS BODY ...))` | `(var NAME (decorate [D ...] (build-class NAME SUPERS BODY ...)))` |
| `(decorate [D ...] (define-global [NAME FORMALS ...] BODY ...))` | `(global NAME (decorate [D ...] (function NAME [FORMALS ...] BODY ...)))` |
| `(decorate [D REST ...] FN)` | `(decorate-subject D (decorate [REST ...] FN))` |

**Behaviour:** What `@` lines read to, and usable directly. The name of
a `def`, `var`, `class`, `global def`, `global class`, `define` or
`define-global` subject is
hoisted out so the decorated value is what gets bound; then the
decorator list folds through
[`decorate-subject`](compile-decorate-subject.md) one decorator per
step, outermost first, so `@d1` over `@d2` over a definition applies as
`(d1 (d2 ...))`, as in Python.

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
