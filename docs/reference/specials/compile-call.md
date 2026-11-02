# compile/call
**Symbol:** `compile/call`

**Syntax:** `(CALLEE ARG ...)`

Argument specifiers (order matters; analogous to Python 3.5+):

| Form        | Syntax     | Example          | Pythonic equivalent |
| ----------- | ---------- | ---------------- | ------------------- |
| Positional  | `EXPR ...` | `(f a b c)`      | `f(a, b, c)`        |
| Keyword     | `KEY: VAL` | `(f x: 1 y: 2)`  | `f(x=1, y=2)`       |
| Star args   | `*: EXPR`  | `(f *: more)`    | `f(*more)`          |
| Star kwargs | `**: EXPR` | `(f **: kwargs)` | `f(**kwargs)`       |

Positionals may interleave with `*:` (star args); keywords may interleave with
`**:` (star kwargs). All positionals and `*:` must come before any keywords or
`**:`; a keyword cannot be followed by `*:` or a positional. A keyword label
written twice in one call is a compile error; a keyword and a `**:` mapping
that name the same key merge at run time, the later one winning. Full form
examples:

```koskari
(fn a b *: more x: xval y: yval **: kwargs)
```

is equivalent to `fn(a, b, *more, x=xval, y=yval, **kwargs)` in Python.

```koskari
(fn a *: bargs c *: dargs)
(fn x: 1 **: kw1 y: 2 **: kw2)
```

are equivalent to `fn(a, *bargs, c, *dargs)` and
`fn(x=1, **kw1, y=2, **kw2)` in Python.

**Use-case:** Generic function application. Evaluates the callee and each
argument, then emits CALL (positional only), CALL_NAMES (positional + keywords),
or CALL_STAR (any use of `*:` or `**:`).

**Behaviour:** If the callee is `(attr OBJ SYM)` or `(attr OBJ SYM)`,
the form expands to `(call-attr OBJ SYM ARGS...)` and that form is
compiled instead. If the callee is a symbol whose name begins with `.` and
there is at least one argument, the first argument is treated as the receiver
`OBJ` and the remainder as `ARGS...`; the leading dot is stripped from the
callee symbol to obtain `SYM`, and the form expands to
`(call-attr OBJ SYM ARGS...)` the same way.

Otherwise, compiles the first subform (the callee), then parses the remaining
forms with `gather_parameters` into positionals and
star-args (interleaved) and keywords and starstar-args (interleaved).
Positional-only calls emit `CALL n`; keyword calls emit `CALL_NAMES`; any star
or starstar use emits `CALL_STAR`. Tail-call mode (`tc`) is respected when set
by the compiler.

**Examples:**

```koskari
;; Positional
(+ 1 2)
(my-func a b c)

;; Keyword (KEY: VAL)
(fn 1 2 name: "x")

;; Star args (*: EXPR) and star kwargs (**: EXPR)
(apply-fn *: args)
(merge *: pos **: kw)
(fn a b *: rest x: 1 **: extra)

;; Interleaved (Python 3.5+ style)
(fn a *: bargs c *: dargs)
(fn x: 1 **: kw1 y: 2 **: kw2)

;; Method call via attr expansion: ((attr obj m) args) -> compile/call-attr
((attr s upper))
((attr obj split sep) "-")

;; Dot-prefix callee: (.m obj args...) -> compile/call-attr (same as attr)
(.upper s)
(.split s "-")
```
