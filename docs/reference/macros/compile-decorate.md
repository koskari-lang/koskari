# compile/decorate
**Syntax:**

- `(decorate [] FN)` — **`FN`**
- `(decorate [DECORATOR ...] (define NAME FORMALS BODY ...))` —
  **`(define NAME (decorate [DECORATOR ...] (function NAME FORMALS BODY ...)))`**
- `(decorate [DECORATOR ...] (define-class NAME SUPERS BODY ...))` —
  **`(define NAME (decorate [DECORATOR ...] (class NAME SUPERS (begin BODY ...))))`**
- `(decorate [D REST ...] FN)` — **`(decorate-subject D (decorate [REST ...] FN))`**

**Behaviour:** Handler for the `@` decorator layout surface, also usable
directly on an s-expr form. Hoists the name out of a **`define`** /
**`define-class`** subject, then folds the decorator list through
[`compile/decorate-subject`](compile-decorate-subject.md) one decorator per
step, outermost decorator first — `@d1` `@d2` over a definition applies as
`(d1 (d2 ...))`, matching Python's ordering.

See [Decorators](../../guide/decorators.md) for the full pipeline from the
`reader/decorators` pragma through layout lowering to expansion and
extension.
