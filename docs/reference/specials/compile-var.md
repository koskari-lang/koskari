# compile/var
**Symbol:** `compile/var`

**Syntax:**

- Declaration only: `(var NAME)`, `(var NAME :: TYPE)`, or
  `(var NAME type: TYPE)`
- Declare and store: `(var NAME VALUE)`, `(var NAME :: TYPE VALUE)`, or
  `(var NAME type: TYPE VALUE)`
- Unpacking: `(var PATTERN VALUE)` or `(var PATTERN)`, where **PATTERN**
  is a bracket list of names, nested lists, and a `*:` rest collector,
  as in [compile/define-values](compile-define-values.md)

**Use-case:** Bind a name in the scope the form appears in: the module at
top level, a function's locals inside a function body, the class
namespace inside a class body. Under the default strict resolution a
store to a name bound in no enclosing scope is a compile error, and
`var` is how a body introduces a local name.

`var` takes a value, never formals and a body. A function definition is
[def](../macros/compile-def.md); `(var f [x] body)` matches no pattern
and fails to compile.

**Behaviour:** Declares **NAME** in the current scope. Without a value
the slot is left empty, and reading it before a store raises. With a
value, evaluates **VALUE** and stores it. In a class body the type
after `::` or `type:` is recorded in the class's `__annotations__`.
The form's value is `None`.

A pattern in the name position lowers to
[compile/define-values](compile-define-values.md), which declares every
name the pattern binds and unpacks **VALUE** into them:

```koskari
(var [a [b c] *: d] x)
;; -> (define-values [a [b c] *: d] x)
```

Without a value, `(var [a b])` declares each name and stores nothing.

**Examples:**

```koskari
;; Declaration only; assign later with set!
(var x)
(set! x 42)

;; Declare and store
(var y 99)

;; Typed
(var port :: int 8080)
(var host type: str "localhost")

;; Unpacking, with a nested pattern and a rest collector
(var [a [b c] *: d] (values 1 (values 2 3) 4 5))
d     ;; (4, 5)
```

In block form:

```koskari
def parse-endpoint [text]:
  var [host port] (text.split ":")
  values host (int port)
```

**See also:** [compile/global](../macros/compile-global.md) to bind at the module
from any depth, [def](../macros/compile-def.md),
[compile/setq-values](compile-setq-values.md),
[Bindings and refs](../../guide/bindings-and-refs.md).
