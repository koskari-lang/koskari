# compile/decorate

**Syntax:** `(decorate [DECORATOR ...] FORM)`

| Form | Expands to |
|------|------------|
| `(decorate [] FN)` | `FN` |
| `(decorate [D ...] (define NAME FORMALS BODY ...))` | `(define NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (define-class NAME SUPERS BODY ...))` | `(define NAME (decorate [D ...] (class NAME SUPERS (begin BODY ...))))` |
| `(decorate [D REST ...] FN)` | `(decorate-subject D (decorate [REST ...] FN))` |

**Behaviour:** What `@` lines read to, and usable directly. The name of
a `define` or `define-class` subject is hoisted out so the decorated
value is what gets bound; then the decorator list folds through
[`decorate-subject`](compile-decorate-subject.md) one decorator per
step, outermost first, so `@d1` over `@d2` over a definition applies as
`(d1 (d2 ...))`, as in Python.

**Examples:**

```koskari
koskari> (macroexpand-1 '(decorate (d1) (define foo [x] x)))
(define foo (decorate [d1] (function foo [x] x)))
```

```koskari
import-from functools lru_cache

@(lru_cache maxsize: None)
@demand
define fib [n]:
  if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))
```

The layout lines read as
`(decorate ((lru_cache maxsize: None) demand) (define fib ...))`. See
[Decorators](../../guide/decorators.md).
