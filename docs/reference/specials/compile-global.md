# compile/global
**Symbol:** `compile/global`

**Syntax:**

- Declaration only: `(global NAME)`, `(global NAME :: TYPE)`, or
  `(global NAME type: TYPE)`
- Declare and store: `(global NAME VALUE)`, `(global NAME :: TYPE VALUE)`,
  or `(global NAME type: TYPE VALUE)`
- Unpacking: `(global PATTERN VALUE)` or `(global PATTERN)`
- Function: `(global def NAME FORMALS BODY ...)`, with an optional
  `:: TYPE` return type after **NAME**
- Class: `(global class NAME SUPERS BODY ...)`
- Syntax: `(global syntax NAME TRANSFORMER)`, with an optional docstring
  before **TRANSFORMER**

**Use-case:** Bind a name at the module from any depth. The value and
declaration shapes are those of [compile/var](compile-var.md); `global`
moves the binding from the scope the form appears in to the module.
Inside a function, `(global NAME)` makes later stores to **NAME** in that
function reach the module binding, as Python's `global` statement does.
Under the default strict resolution a store to a name bound in no
enclosing scope is a compile error, and `global` is how a function says
it means the module's name.

**Behaviour:** Records **NAME** as a module binding for the current
scope, then, with a value, evaluates **VALUE** and stores it at the
module. A name the same scope already declared with `var` cannot also be
declared `global`. The form's value is `None`.

A pattern in the name position binds each name the pattern binds at the
module, then unpacks **VALUE** into them:

```koskari
(global [a b] x)
;; -> (begin (global a) (global b) (setq-values [a b] x))
```

Followed by `def`, `class`, or `syntax`, `global` binds that definition
at the module rather than in the scope the form appears in:

```koskari
(global def f [x] body ...)
;; -> (global f (function f [x] body ...))

(global class C [Base] body ...)
;; -> (global C (build-class C [Base] body ...))

(global syntax twice (syntax-rules [] ...))
;; -> (define-syntax twice (syntax-rules [] ...))
```

`def`, `class`, and `syntax` are literals in this position, so
`(global def)` never names a variable called `def`. A decorator line
above `global def` or `global class` decorates the function or class and
binds the result at the module.

**Examples:**

```koskari
var server None

def start []:
  global server
  when (None? server):
    set! server (create-server)
  (server.serve)
```

Declare and assign in one step:

```koskari
var count 5

def zero-count []:
  global count 0

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

**See also:** [compile/var](compile-var.md),
[compile/define-global](compile-define-global.md),
[def](../macros/compile-def.md),
[compile/build-class](compile-build-class.md),
[syntax](../macros/compile-syntax.md),
[Bindings and refs](../../guide/bindings-and-refs.md), the
`compiler/resolution` setting in [Pragmas](../../guide/pragma.md).
