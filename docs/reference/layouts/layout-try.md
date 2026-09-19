# layout/try
**Symbol:** `layout/try`

**Use-case:** try/except/else/finally block syntax. Lowers to `(try ...)` for
compilation. Requires at least one `except` or `finally` peer.

**Behaviour:** Accepts peer blocks `except`, `else`, and `finally`. Produces
`(try (begin BODY) CLAUSE...)`. The `except`, `else`, and `finally` clauses all
follow the body as elements of the `try` form, in the order they were written.
Rules: `else` and `finally` take no arguments; multiple `else` or
`finally` are not allowed; at least one except or finally is required.

Three forms of `except` peer are accepted:

- **Bare** -- `except:` with no arguments: catches any exception without a type
  check and without binding. Emits `(except: BODY...)`.
- **Type-only** -- `except [TYPEEXPR]:` with a single bracketed USE form
  containing only the type expression: checks the exception type, no binding.
  Emits `([TYPEEXPR] BODY...)`.
- **Binding** -- `except [SYM TYPEEXPR]:` or `except [TYPEEXPR as: SYM]:`:
  checks the type and binds the caught exception to `SYM`. Emits
  `([SYM TYPEEXPR] BODY...)` or `([TYPEEXPR as: SYM] BODY...)`.

The USE form is validated but not rewritten: whichever of the two binding
spellings was written is the one that reaches the `try` form. All typed and
binding forms use bracketed USE specifiers; `except TYPEEXPR:` (no brackets)
is invalid.

**Syntax:**

```
try:
  BODY
except:
  HANDLER
except [TYPEEXPR]:
  HANDLER
except [SYM TYPEEXPR]:
  HANDLER
except [TYPEEXPR as: SYM]:
  HANDLER
else:
  ELSE-BODY
finally:
  FINALLY-BODY
```

**Examples:**

```koskari
try:
  x
except [Exception]:
  y
;; -> (try (begin x) ([Exception] y))

try:
  x
except [e Exception]:
  y
;; -> (try (begin x) ([e Exception] y))

try:
  x
except [Exception as: e]:
  y
;; -> (try (begin x) ([Exception as: e] y))

try:
  x
except:
  y
;; -> (try (begin x) (except: y))

try:
  x
finally:
  z
;; -> (try (begin x) (finally: z))

try:
  x
except [ValueError]:
  y
except [TypeError]:
  w
;; -> (try (begin x) ([ValueError] y) ([TypeError] w))

try:
  x
except [Exception]:
  y
else:
  a
finally:
  z
;; -> (try (begin x) ([Exception] y) (else: a) (finally: z))
```
