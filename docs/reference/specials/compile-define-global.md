# compile/define-global
**Symbol:** `compile/define-global`

`define-global` is the Scheme-layer spelling of
[`global`](compile-global.md) and `global def`.

**Syntax:** The arms of [compile/define](compile-define.md), binding at
the module:

- Function: `(define-global [NAME FORMALS ...] BODY ...)` or
  `(define-global (NAME FORMALS ...) BODY ...)`
- Declaration only: `(define-global NAME)`, `(define-global NAME :: TYPE)`,
  or `(define-global NAME type: TYPE)`
- Declare and store: `(define-global NAME VALUE)`,
  `(define-global NAME :: TYPE VALUE)`, or
  `(define-global NAME type: TYPE VALUE)`

A pair in the name position is a function head, its first element the
name and the rest the formals. A symbol is a value binding.

**Use-case:** Bind a name at the module from any depth. Inside a
function body, `define-global` makes **NAME** refer to the module
binding throughout that body, as Python's `global` statement does, so
reads, `set!`, and `setq` of **NAME** reach the module. At module top
level it binds as [`define`](compile-define.md) does.

**Behaviour:** A function head stores
`(function NAME FORMALS ... BODY ...)` into the module binding
**NAME**, as `(global def NAME [FORMALS ...] BODY ...)` does. A symbol
binds the module name as [`global`](compile-global.md) does: with a
value it evaluates and stores it, without one it stores nothing. The
form's value is `None`.

A name declared local in the same body, with `var` or `define`, cannot
also be made global there; the compiler raises `CompilerError`.

The pre-Scheme shorthand `(define-global NAME [FORMALS ...] BODY ...)`
matches no rule and raises `SyntaxMatchError`. `(define-global [a b] x)`
defines a function named `a`, as with [`define`](compile-define.md);
it does not unpack.

**Examples:**

```koskari
;; Explicit global; setq writes to module
(define-global x 1)
(setq x 42)

;; A function bound at the module from inside another function
(define [install]
  (define-global [helper x] (* x 10))
  None)
(install)
(helper 4)    ;; 40

;; Shared counter across nested closures
((lambda ()
   (define-global counter 0)
   (define [inc]
     (setq counter (+ counter 1))
     counter)
   (inc)))
```

**See also:** [compile/global](compile-global.md),
[compile/define](compile-define.md), [def](../macros/compile-def.md).
