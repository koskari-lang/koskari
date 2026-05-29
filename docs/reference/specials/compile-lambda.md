# compile/lambda

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/lambda`

**Syntax** (most specific forms first; `NAME` is optional in all groups):


| Shape                        | Form                                               |
| ---------------------------- | -------------------------------------------------- |
| Named + return               | `(compile/lambda NAME :: RET FORMALS BODY ...)`    |
| Named + return (`type:`)     | `(compile/lambda NAME type: RET FORMALS BODY ...)` |
| Anonymous + return           | `(compile/lambda :: RET FORMALS BODY ...)`         |
| Anonymous + return (`type:`) | `(compile/lambda type: RET FORMALS BODY ...)`      |
| Named, no return             | `(compile/lambda NAME FORMALS BODY ...)`           |
| Anonymous, no return         | `(compile/lambda FORMALS BODY ...)`                |


`RET` is a single expression (not a formal parameter). It is recorded as compiler
metadata on the resulting `Code` (`code.extras['return']`) and exposed on the
callable as `__annotations__['return']` (via `kos_typehint_to_dunder_annotation`
in the runtime — same string rules as parameter annotations, e.g. simple
symbols become their name strings).

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b c: default-c]`, or a single symbol
for rest. Supports `*` and `**` in formals (see `gather_formals` in helpers and
the **FORMALS** composite in [MATCHER.md](../../design/MATCHER.md)).
Parameter types may use `::` / `type:` on individual parameters (see formals
helpers).

**Docstring:** If the first form in `BODY` is a string literal, it is taken as
the function docstring (`discover_docstring` in helpers). It is attached to the
fragment, stored on `Code` as `code.extras['doc']`, and appears on the callable
as `__doc__`. A leading `(compile/begin ...)` in the body is peeled only for
docstring discovery; the body is still compiled in full.

**Use-case:** Create an anonymous or named function value. Named lambdas bind the
given name in the function's scope so the function can refer to itself (e.g. for
recursion).

**Behaviour:** Matches one of the shapes above, parses formals with
`gather_formals`, builds a fragment that declares the parameter names, compiles
the body with `helper_begin`, and completes it as a function. If a return type
was present, sets `code.extras['return']` from the `RET` form after `complete`.
Pushes a `FunctionSpec` constant and emits `BUILD_FUNCTION`. Default values for
keyword parameters are compiled in the enclosing scope and passed in via the
constant tuple for defaulted keywords.

**Examples:**

```scheme
;; Anonymous identity
(compile/lambda [x] x)

;; Anonymous two-argument function
(compile/lambda [a b] (compile/call add a b))

;; Named lambda for recursion
(compile/lambda fact [n]
  (compile/call fact ...))

;; Keyword parameter with default
(compile/lambda [x flag: true] ...)

;; Return type (named and anonymous)
(compile/lambda f :: int [x] x)
(compile/lambda :: str [x] "hi")

;; Docstring + return type
(compile/lambda g :: int [x] "doc for g" x)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
