# compile/define
**Symbol:** `compile/define`

`define` is the Scheme-layer spelling. Koskari code binds a value with
[`var`](compile-var.md) and a function with
[`def`](../macros/compile-def.md); see
[Bindings and refs](../../guide/bindings-and-refs.md).

**Syntax:**

- Function: `(define [NAME FORMALS ...] BODY ...)` or
  `(define (NAME FORMALS ...) BODY ...)`
- Declaration only: `(define NAME)`, `(define NAME :: TYPE)`, or
  `(define NAME type: TYPE)`
- Declare and store: `(define NAME VALUE)`, `(define NAME :: TYPE VALUE)`,
  or `(define NAME type: TYPE VALUE)`

The form of the first operand selects the arm. A pair, `[ ]` or `( )`,
is a function head: its first element is the name and the rest are the
formals, in every shape [`lambda`](compile-lambda.md) accepts. A symbol
is a value binding.

In block form the function head is followed by a colon and an indented
body:

```koskari
define [NAME FORMALS ...]:
  BODY ...
```

**Use-case:** Scheme-style definitions. `(define [f x] body)` reads as
Scheme's `(define (f x) body)`; `(define x v)` is
`(var x v)`.

**Behaviour:** A function head declares **NAME** in the current scope
and stores `(function NAME FORMALS ... BODY ...)` into it, as
`(def NAME [FORMALS ...] BODY ...)` does. The function needs at least
one body form. A recursive call to **NAME** inside the body reads the
binding in the enclosing scope.

A symbol in the name position behaves as [`var`](compile-var.md):
**NAME** is declared in the current scope, **VALUE**, when present, is
evaluated and stored, and without a value the slot is left empty, so
reading it before a store raises. The form's value is `None`.

`define` has no return-type slot for a function. A typed function is
`(def NAME :: TYPE [FORMALS ...] BODY ...)`, or
`(define NAME (function NAME :: TYPE [FORMALS ...] BODY ...))`.

A pair in the name position whose head is not a symbol raises
`SpecialError`. The pre-Scheme shorthand `(define NAME [FORMALS ...] BODY ...)`
matches no rule and raises `SyntaxMatchError`; move the name inside the
pair.

**Pitfall:** `(define [a b] x)` is not an unpacking. It defines a
function named `a` with one formal `b` whose body is `x`, and binds
nothing named `b`:

```koskari
(define [a b] #(1 2))
(a "ignored")          ;; (1, 2)
```

Unpack with [`var`](compile-var.md) over a pattern, or with
[`define-values`](compile-define-values.md):

```koskari
(var [a b] #(1 2))
(define-values [a b] #(1 2))
```

A decorator line above a function `define` decorates the function and
binds the result; see [`decorate`](../macros/compile-decorate.md).

**Examples:**

```koskari
;; Declaration only; assign later with set!
(define x)
(set! x 42)

;; Typed declaration and value
(define port :: int)
(define host type: str "localhost")

;; Declare and store
(define y 99)

;; Function, with ( ) or [ ] around the head
(define (square x) (* x x))
(define [double x] (* x 2))
(define [fact n]
  (cond [(== n 0) 1]
        [else: (* n (fact (- n 1)))]))

;; Typed function value
(define half (function half :: float [x] (/ x 2)))
```

In block form, with a decorator:

```koskari
define [twice f]:
  (lambda [x] (* 2 (f x)))

@twice
define [inc x]:
  (+ x 1)

(inc 4)    ;; 10
```

**See also:** [compile/define-global](compile-define-global.md),
[compile/var](compile-var.md), [def](../macros/compile-def.md),
[compile/function](compile-function.md),
[compile/define-values](compile-define-values.md).
