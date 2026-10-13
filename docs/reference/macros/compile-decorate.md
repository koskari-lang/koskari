# compile/decorate

**Syntax:** `(decorate [DECORATOR ...] FORM)`

| Form | Expands to |
|------|------------|
| `(decorate [] FN)` | `FN` |
| `(decorate [D ...] (def NAME FORMALS BODY ...))` | `(var NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (var NAME VALUE))` | `(var NAME (decorate [D ...] VALUE))` |
| `(decorate [D ...] (global def NAME FORMALS BODY ...))` | `(global NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (global class NAME SUPERS BODY ...))` | `(global NAME (decorate [D ...] (build-class NAME SUPERS BODY ...)))` |
| `(decorate [D ...] (define NAME FORMALS BODY ...))` | `(define NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (define-class NAME SUPERS BODY ...))` | `(define NAME (decorate [D ...] (class NAME SUPERS (begin BODY ...))))` |
| `(decorate [D ...] (define-global NAME FORMALS BODY ...))` | `(define-global NAME (decorate [D ...] (function NAME FORMALS BODY ...)))` |
| `(decorate [D ...] (define-demanding NAME FORMALS BODY ...))` | `(define NAME (decorate [D ...] (demand (function NAME FORMALS BODY ...))))` |
| `(decorate [D ...] (define-coroutine NAME FORMALS BODY ...))` | `(define NAME (decorate [D ...] (coroutine-fn (function NAME FORMALS BODY ...))))` |
| `(decorate [D ...] (define-dataclass NAME SUPERS MEMBERS ...))` | `(define NAME (decorate [D ...] (dataclass (class NAME SUPERS ...))))` followed by the `NAME?` predicate |
| `(decorate [D REST ...] FN)` | `(decorate-subject D (decorate [REST ...] FN))` |

**Behaviour:** What `@` lines read to, and usable directly. The name of
a `def`, `var`, `global def`, `global class`, `define`, `define-class`,
`define-global`, `define-demanding`, `define-coroutine` or
`define-dataclass` subject is hoisted out so the
decorated value is what gets bound; then the decorator list folds through
[`decorate-subject`](compile-decorate-subject.md) one decorator per
step, outermost first, so `@d1` over `@d2` over a definition applies as
`(d1 (d2 ...))`, as in Python.

**Examples:**

```koskari
>>> (macroexpand-1 '(decorate (d1) (define foo [x] x)))
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
