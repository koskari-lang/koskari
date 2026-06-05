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


`RET` is a single expression (not a formal parameter). It is recorded as compiler
metadata on the resulting `Code` (`code.extras['return']`) and exposed on the
callable as `__annotations__['return']` (via `kos_typehint_to_dunder_annotation`
in the runtime — same string rules as parameter annotations, e.g. simple
symbols become their name strings).

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b c: default-c]`, or a single symbol
for rest. Supports `*` and `**` in formals (see `gather_formals` in helpers and
[`koskari.matcher`](../../api/koskari-matcher.md)).
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

**See also:** [compile/function](compile-function.md) — named functions **without**
a closure self-binding at creation; used by `define` / `define-global` function
shorthand and decorator macros.

**Behaviour:** Matches one of the shapes above, parses formals with
`gather_formals`, builds a fragment that declares the parameter names, compiles
the body with `helper_begin`, and completes it as a function. If a return type
was present, sets `code.extras['return']` from the `RET` form after `complete`.
Pushes a `FunctionSpec` constant and emits `BUILD_FUNCTION`. Default values for
keyword parameters are compiled in the enclosing scope and passed in via the
constant tuple for defaulted keywords.

**Examples:**

```koskari
;; Anonymous identity
(lambda [x] x)

;; Anonymous two-argument function
(lambda [a b] (add a b))

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

