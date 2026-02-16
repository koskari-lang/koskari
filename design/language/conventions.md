# Koskari language conventions

Recommended style for Koskari Lisp source code. These are conventions for
readability and consistency; the parser and compiler accept equivalent forms
either way.


## Keywords

Keywords are atomic symbols that start or end with a colon. These all denote
the same keyword (e.g. `keyword("foo")`):

- `:foo`
- `foo:`
- `:foo:`

### Named arguments

When a keyword denotes a **named argument** (parameter name plus value), put the
colon at the **end**:

```scheme
(fn 1 2 name: "alice" count: 3)
```

So: `name: bar` not `:name bar` for the argument name.

### Standalone flags

When a keyword is used as a **standalone flag** (no following value), put the
colon at the **beginning**, or on both sides:

```scheme
:global:
:optional
```

### Star and starstar

Rest-argument markers are keywords. Use `*:` for variadic positional and `**:`
for variadic keyword in formals and call sites:

```scheme
(lambda [a b *: rest] ...)
(foo *: args **: kwds)
```

So formals and calls use `*:` and `**:` rather than bare `*` and `**` for
consistency with the keyword convention.


## Brackets for non-evaluation lists

In source code, use **square brackets `[]`** for lists that are **data** — that
is, lists that are not evaluated as special forms or as function calls. Use
**parentheses `()`** for forms that are evaluated (calls and special-form
invocations).

Examples of where brackets are recommended:

- **Formals** in lambda or function definitions: `(lambda [a b :c default] ...)`
- **Bindings** in let: `(let [x 1 y 2] body)`
- **Clause heads** in cond (the condition part of each branch): `(cond [p1 e1] [p2 e2] else: e3)`
- Any other list that is “structure” rather than “code”

This is **purely for legibility**. Both `[]` and `()` are represented as cons
pairs; the reader turns brackets into the same underlying structure. The
convention helps readers (and tools) distinguish “this list is data” from “this
list is a call or special form” at a glance.
