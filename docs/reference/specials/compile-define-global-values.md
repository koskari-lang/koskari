# compile/define-global-values
**Symbol:** `compile/define-global-values`

`define-global-values` is the special that
[`global var`](../macros/compile-global.md) over a pattern lowers to.

**Syntax:** `(define-global-values PATTERN VALUE)` or
`(define-global-values PATTERN)`

Same PATTERN shapes as [compile/define-values](compile-define-values.md):
a list of names, nested lists, and one `*:` rest collector; with a
**VALUE**, a single name as well. Without **VALUE** the names are bound
at the module and nothing is stored; a single name without a value is
[compile/define-global](compile-define-global.md).

**Use-case:** Bind several module names at once from inside any scope.
Where [compile/define-values](compile-define-values.md) declares its
names in the scope the form appears in, this form binds them at the
module, as [compile/define-global](compile-define-global.md) does for
one name.

**Behaviour:** Records every name the pattern binds as a module binding
for the current scope, compiles **VALUE**, then unpacks it into those
names. A name the same scope already declared with `var` cannot also be
bound here; the compiler raises `CompilerError`. The form's value is
`None`.

**Examples:**

```koskari
(define-global-values [lo hi] (values 1 9))

(define [setup]
  (define-global-values [first *: rest] (range 5))
  None)
(setup)
rest    ;; (1, 2, 3, 4)
```

**See also:** [compile/define-values](compile-define-values.md),
[compile/define-global](compile-define-global.md),
[global](../macros/compile-global.md).
