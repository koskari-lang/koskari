# layout/try
**Symbol:** `layout/try`

**Use-case:** try/except/else/finally block syntax. Lowers to `(try ...)` for
compilation. Requires at least one `except` or `finally` peer.

**Behaviour:** Accepts peer blocks `except`, `else`, and `finally`. Produces
`(try (compile/begin BODY) CATCHES...)` where CATCHES is the list of except,
else, and finally clauses. Rules: `else` and `finally` take no arguments;
multiple `else` or `finally` are not allowed; at least one except or finally
is required.

Three forms of `except` peer are accepted:

- **Bare** — `except:` with no arguments: catches any exception without a type
  check and without binding. Emits `(except: BODY...)`.
- **Type-only** — `except [TYPEEXPR]:` with a single bracketed USE form
  containing only the type expression: checks the exception type, no binding.
  Emits `(TYPEEXPR BODY...)`.
- **Binding** — `except [SYM TYPEEXPR]:` or `except [TYPEEXPR as: SYM]:`:
  checks the type and binds the caught exception to `SYM`. Emits
  `((SYM . TYPEEXPR) BODY...)`.

All typed and binding forms use bracketed USE specifiers; `except TYPEEXPR:`
(no brackets) is invalid.

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
;; → (try (compile/begin x) [[Exception] y])

try:
  x
except [e Exception]:
  y
;; → (try (compile/begin x) [[e Exception] y])

try:
  x
except [Exception as: e]:
  y
;; → (try (compile/begin x) [[e  Exception] y])

try:
  x
except:
  y
;; → (try (compile/begin x) [except: y])

try:
  x
finally:
  z
;; → (try (compile/begin x) [finally: z])

try:
  x
except [Exception]:
  y
else:
  a
finally:
  z
;; → (try (compile/begin x) [[Exception] y] [else: a] [finally: z])
```

**Implementation:** `koskari/reader/layouts/exceptions.layout_try`


