# compile/decorate
**Syntax:**

- `(decorate [] FN)`
- `(decorate [DECORATOR ...] (define-class NAME SUPERS BODY ...))`
- `(decorate [DECORATOR ...] (define NAME FORMALS BODY ...))`
- `(decorate [DECORATOR ...] FN)` — **`((function-compose DECORATOR ...) FN)`**

**Behaviour:** Wraps **`class`** / **`lambda`** inside **`define`** / **`define-class`**, or
composes decorators for a bare callable.
