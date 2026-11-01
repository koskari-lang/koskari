# compile/lambda
**Symbol:** `compile/lambda`

**Syntax** (most specific forms first; `NAME` is optional in all groups):

| Shape                        | Form                                               |
| ---------------------------- | -------------------------------------------------- |
| Named + return               | `(lambda NAME :: RET FORMALS BODY ...)`    |
| Named + return (`type:`)     | `(lambda NAME type: RET FORMALS BODY ...)` |
| Anonymous + return           | `(lambda :: RET FORMALS BODY ...)`         |
| Anonymous + return (`type:`) | `(lambda type: RET FORMALS BODY ...)`      |
| Named, no return             | `(lambda NAME FORMALS BODY ...)`           |
| Anonymous, no return         | `(lambda FORMALS BODY ...)`                |

`RET` is a single expression (not a formal parameter). Like parameter
annotations, it is evaluated in the **defining scope** when the function is
created, and appears on the callable as `__annotations__['return']` as a real
object -- `:: int` yields the `int` type itself, and
`inspect.signature(fn).return_annotation` matches. An unbound name raises
`NameError` at function creation, just like an unbound keyword default.

A **string literal** annotation (`:: "Request"`, `:: "pkg.Request"`) is the
forward-reference escape hatch: it is kept as a string and resolved lazily
(dotted lookup against the defining namespace) the first time
`__annotations__` or `__signature__` is read. A name that fails to resolve
stays a string and is not retried.

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b c: default-c]`, or a single symbol
for rest. A positional parameter takes a default as a `(name default)` cell,
`[a (b 10)]`; such cells follow the required positionals, as in Python, and a
required name after one is a compile error, as is a name that appears twice
in one formals list. Supports `*` and `**` in formals
(see `gather_formals` in helpers and
[`koskari.matcher`](../api/koskari-matcher.md)).
Parameter types may use `::` / `type:` on individual parameters (see formals
helpers).

**Docstring:** If the first form in `BODY` is a string literal, it is taken as
the function docstring (`discover_docstring` in helpers). It is attached to the
fragment, stored on `Code` as `code.extras['doc']`, and appears on the callable
as `__doc__`. A leading `(begin ...)` in the body is peeled only for
docstring discovery; the body is still compiled in full.

**Use-case:** Create an anonymous or named function value. Named lambdas bind the
given name in the function's scope so the function can refer to itself (e.g. for
recursion in a named `let` or inline `(lambda NAME ...)`).

**See also:** [compile/function](compile-function.md) -- named functions **without**
a closure self-binding at creation; used by `def`, `global def`, and decorator
macros.

**Behaviour:** Matches one of the shapes above, parses formals with
`gather_formals`, builds a fragment that declares the parameter names, compiles
the body with `helper_begin`, and completes it as a function. Annotation
expressions (parameter types and `RET`) and keyword-default values are
compiled in the enclosing scope; their values are consumed at function
creation by `BUILD_FUNCTION`, which builds the keyword-defaults dict and the
`__annotations__` dict from name tuples carried on the instruction (see
[KOS_OP_BUILD_FUNCTION](../vm/opcodes/kos-op-build-function.md)).

**Examples:**

```koskari
;; Anonymous identity
(lambda [x] x)

;; Anonymous two-argument function
(lambda [a b] (+ a b))

;; Named lambda for recursion
(lambda fact [n]
  (fact ...))

;; Keyword parameter with default
(lambda [x flag: true] ...)

;; Return type (named and anonymous)
(lambda f :: int [x] x)
(lambda :: str [x] "hi")

;; Docstring + return type
(lambda g :: int [x] "doc for g" x)
```
