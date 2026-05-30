# compile/define
**Symbol:** `compile/define`

**Syntax:** The name and optional return type can be written in either of two
ways:

**Inline type** (preferred; same `::` / `type:` placement as `compile/lambda`):

- Declaration only: `(define NAME :: TYPE)` or `(define NAME type: TYPE)`
- Declare and store: `(define NAME :: TYPE EXPR)` (or `type: TYPE`)
- Function shorthand:
`(define NAME :: TYPE FORMALS BODY ...)` (or `type: TYPE`)

**DECL** as the first argument (single binding-shaped form, no value):

- Declaration only: `(define DECL)`
- Declare and store: `(define DECL EXPR)`
- Function shorthand: `(define DECL FORMALS BODY ...)`

**DECL** may be:

- A **symbol** (e.g. `x`), or
- A **proper list** introducing a name with optional **type** attributes only:
e.g. `[x]`, `[x :: int]`, `[x type: int]`, or `(TYPE as: NAME)` for the
type-as-name spelling. **`value:`** and two-element **`(SYM EXPR)`** defaults
are rejected (declarations do not carry a value).

**Use-case:** Variable and function definition for module-level loading.
Declaration-only leaves the ref in scope but empty (reading before assignment
raises). Declare-and-store evaluates EXPR and stores the result in the slot
(equivalent to compile/bindq). Function shorthand lowers to a named
`compile/lambda`: if a return type is present (inline or in `DECL`), the
expansion is `(lambda NAME :: TYPE FORMALS BODY ...)` (using the `::`
keyword); if not, `(lambda NAME FORMALS BODY ...)`.

For declaration-only or value binding with a type (e.g.
`(define [x :: int])` or `(define x :: int 99)`), the type is
accepted by the parser but is not stored on the variable binding in the current
compiler (parse-only / metadata for tooling), except that function shorthand
passes the type through to `compile/lambda` as the return annotation.

**Behaviour:** With **DECL**, parses the first argument with `parse_decl`. With
**inline type**, the matcher binds `NAME`, `::` or `type:`, and `TYPE` (see
MATCHER.md — **DECL** composite). Declares
the introduced name. For declaration-only: emits CONST None. For
declare-and-store: compiles EXPR, emits `VAR_STORE` to store the value, then
CONST None. For function shorthand: builds the `compile/lambda` form as above and
compiles it, then emits `VAR_STORE`.

**Examples:**

```koskari
;; Declaration only; assign later with compile/setq
(define x)
(setq x 42)

;; Typed declaration (parse-only on the binding; assign later as usual)
(define x :: int)
(define [x :: int])

;; Declare and store
(define y 99)

;; Function shorthand
(define double [x] (* x 2))
(define f :: int [x] 5)
(define [f :: int] [x] 5)
(define fact [n]
  (cond [(eq n 0) 1]
                [else: (* n (fact (sub n 1)))]))
```


