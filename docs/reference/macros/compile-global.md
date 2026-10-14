# compile/global
**Symbol:** `compile/global`

**Syntax:** `global` is a prefix over a binding form. The form after it
is bound at the module instead of in the scope the whole form appears
in.

- Value: `(global var NAME)`, `(global var NAME :: TYPE)`,
  `(global var NAME type: TYPE)`, `(global var NAME VALUE)`,
  `(global var NAME :: TYPE VALUE)`, or
  `(global var NAME type: TYPE VALUE)`
- Unpacking: `(global var PATTERN VALUE)` or `(global var PATTERN)`
- Function: `(global def NAME FORMALS BODY ...)`, with an optional
  `:: TYPE` return type after **NAME**
- Class: `(global class NAME SUPERS BODY ...)`
- Syntax: `(global syntax NAME TRANSFORMER)`, with an optional docstring
  before **TRANSFORMER**

`var`, `def`, `class`, and `syntax` are literals in this position, so
`(global var x)` can only mean the module's `x`, never a variable called
`var`. A bare name after `global`, `(global x 1)`, matches no rule and
raises `SyntaxMatchError`.

**Use-case:** Bind a name at the module from any depth. The shapes after
`global var` are those of [compile/var](../specials/compile-var.md).
Inside a function, `(global var NAME)` makes later stores to **NAME** in
that function reach the module binding. Under the default strict
resolution a store to a name bound in no enclosing scope is a compile
error, and `global var` is how a function says it means the module's
name.

**Behaviour:** Every arm lowers to
[compile/define-global](../specials/compile-define-global.md), or to
[compile/define-global-values](../specials/compile-define-global-values.md)
for a pattern. Each records its names as module bindings for the
current scope and, with a value, evaluates it and stores it at the
module. A name the same scope already declared with `var` cannot also
be declared `global var`. The form's value is `None`.

```koskari
(global var x 1)
;; -> (define-global x 1)

(global var [a b] x)
;; -> (define-global-values [a b] x)

(global def f [x] body ...)
;; -> (define-global f (function f [x] body ...))

(global class C [Base] body ...)
;; -> (define-global C (build-class C [Base] body ...))

(global syntax twice (syntax-rules [] ...))
;; -> (define-syntax twice (syntax-rules [] ...))
```

A decorator line above `global var`, `global def` or `global class`
decorates the value, function or class and binds the result at the
module.

**Examples:**

```koskari
var server None

def start []:
  global var server
  when (None? server):
    set! server (create-server)
  (server.serve)
```

Declare and assign in one step:

```koskari
var count 5

def zero-count []:
  global var count 0

(zero-count)
count     ;; 0
```

A function and a class defined from inside a function body:

```koskari
def setup []:
  global def square [x]:
    * x x

  global class Point []:
    var x 0
    var y 0

(setup)
square 4  ;; 16
```

**See also:** [compile/define-global](../specials/compile-define-global.md),
[compile/var](../specials/compile-var.md),
[def](compile-def.md),
[compile/build-class](../specials/compile-build-class.md),
[syntax](compile-syntax.md),
[Bindings and refs](../../guide/bindings-and-refs.md), the
`compiler/resolution` setting in [Pragmas](../../guide/pragma.md).
