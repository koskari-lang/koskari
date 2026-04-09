# Koskari Macros — Implemented Reference

This document describes the macros currently implemented in Koskari. Macros
expand at compile time before the compiler sees the result; the expanded forms
are then compiled as normal. For compiler special forms, see
[SPECIALS.md](SPECIALS.md).

User-facing **`define-syntax`** is not implemented as a compiler special: it is
handled by the **`compile/define-syntax`** macro in `koskari/importer/basics.ki`
(bound with `(define compile/define-syntax (syntax-rules …))`). That macro
rewrites `(define-syntax NAME TRANSFORMER)` into **`(compile/define NAME …)`**
with a transformer whose head is **`syntax-rules`**, **`macro-rules`**,
**`syntax-match`**, or **`macro-match`**. The compiler then lowers those
**`syntax-rules` / `macro-rules` / …** specs via the specials **`compile/syntax-rules`**,
**`compile/macro-rules`**, **`compile/syntax-match`**, and **`compile/macro-match`**
(see [SPECIALS.md](SPECIALS.md)), and binds the result with **`compile/define`**.

**Design (living):** [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md) — matcher IR, **RULE** vs **MATCH** pattern languages (see that doc: **`syntax-rules`** / **`macro-rules`** vs **`syntax-match`** / **`macro-match`**), and how `define-syntax` / future `define-macro` share the transformer pipeline with Python specials.

Macros documented here are defined in `koskari/importer/basics.ki` and are
available in every module without import. The format mirrors SPECIALS.md to
ease future migration of purely transformative forms from specials to macros.


## compile/define-syntax (macro)

**Symbol:** `compile/define-syntax` — the macro that defines other macros.

**Syntax:** The macro is defined in `basics.ki` and matches **`(define-syntax …)`**
forms whose transformer is one of:

- `(syntax-rules literals rule …)` → `(compile/define name (syntax-rules name literals rule …))`
- `(syntax-match rule …)` → `(compile/define name (syntax-match name rule …))`
- `(macro-rules literals rule …)` → `(compile/define name (macro-rules name literals rule …))`
- `(macro-match rule …)` → `(compile/define name (macro-match name rule …))`

**Use-case:** Install a compile-time transformer under the given name. After
expansion, the **value** of **`compile/define`** is built from the **`syntax-rules`** /
**`macro-rules`** / … spec (see specials above).

**Behaviour:** Purely macro expansion; no dedicated `@special` handler for
`compile/define-syntax` in the compiler.

**Defined in:** `koskari/importer/basics.ki`

**Tests:** `tests/importer/test_basics.py` (where applicable); compile specials
for **`compile/syntax-rules`** / **`compile/macro-rules`**: `tests/compile/specials/test_define_syntax.py`


## set!

**Symbol:** `set!` (defined as `compile/set!`)

**Syntax:**
- Plain assignment: `(set! SYM EXPR)`
- Multi-value / unpack assignment: `(set! (values PATTERN ...) VALUE)` — same
  pattern shapes as `compile/for-each` unpack bindings (including `*:`
  segments)
- Augmented assignment: `(set! SYM (OP SYM EXPR))` where OP is `+`, `-`, `*`,
  `/`, `//`, `%`, `**`, `@`, `<<`, `>>`, `&`, `^`, or `|`
- Item assignment: `(set! (item OBJ KEY) VAL)` or
  `(set! (compile/item OBJ KEY) VAL)`
- Attribute assignment: `(set! (attr OBJ NAME) VAL)` or
  `(set! (compile/attr OBJ NAME) VAL)`
- Ref assignment: `(set! (deref REF) VAL)` or `(set! (compile/deref REF) VAL)`

**Use-case:** User-facing assignment syntax. Covers variable store, unpack into
several existing variables, augmented assignment, subscript store, attribute
store, and mutable reference store.

**Behaviour:** Expands via syntax-rules. Plain assignment expands to
`(setq NAME VALUE)`. `(set! (values pattern ...) value)` expands to
`(compile/setq-values [pattern ...] value)` (documented in
[SPECIALS.md](SPECIALS.md) as compile/setq-values). Augmented forms like `(set! x (+ x 1))` expand to
`(compile/inplace OP NAME VALUE)`. Item, attribute, and deref forms (including
`compile/item`, `compile/attr`, `compile/deref` variants) expand to
`(compile/set-item ...)`, `(compile/set-attr ...)`, and `(compile/set-ref ...)`
respectively. The first match in the syntax-rules wins.

**Examples:**

```scheme
;; Plain assignment
(set! x 42)
(set! counter (add counter 1))

;; Unpack into existing variables
(set! (values a b) pair)

;; Augmented assignment
(set! x (+ x 1))      ; x += 1
(set! n (* n 2))      ; n *= 2
(set! bits (| bits mask))

;; Item and attribute
(set! (item vec 0) 99)
(set! (attr obj count) 0)

;; Mutable reference
(set! (deref r) 100)
```

**Defined in:** `koskari/importer/basics.ki`


## let-values

**Symbol:** `let-values` (defined as `compile/let-values`)

**Syntax:** `(let-values [PATTERN VALUE] body ...)`

PATTERN is a symbol or list unpack pattern (same shapes as `set!` with
`values`, including `*:` rest segments). VALUE is evaluated once; bindings are
visible only in `body ...`.

**Use-case:** Lexical multi-value binding: introduce fresh names from a single
evaluated value for a local scope.

**Behaviour:** Expands to a `let` that evaluates the value expression once,
binds it to `resolved`, then `(compile/define-values PATTERN resolved)` before
`body ...`. See [SPECIALS.md](SPECIALS.md) § compile/define-values for the
special form.

**Examples:**

```scheme
(let-values [[a b] pair]
  (use a b))

(let-values [(x *: ys z) seq]
  (frobnicate x ys z))
```

**Defined in:** `koskari/importer/basics.ki`


## let*

**Symbol:** `let*` (defined as `compile/let*`)

**Syntax:** `(let* [[NAME INIT] ...] body ...)`

Each INIT is evaluated in order; later bindings see earlier names. Same binding
shape as `compile/let` (see [SPECIALS.md](SPECIALS.md) § compile/let).

**Use-case:** Sequential `let`: nested scope where each RHS may use prior
bound names.

**Behaviour:** Expands by syntax-rules to nested `let`. Empty bindings:
`(let [] body ...)`. A single binding: `(let [first] body ...)` (avoids a
redundant inner `(let [] ...)`). Two or more:
`(let [first] (let* [rest ...] body ...))`.

**Examples:**

```scheme
(let* [] 1)

(let* [[x 1]] x)

(let* [[x 1] [y (+ x 1)]] y)
```

**Defined in:** `koskari/importer/basics.ki`


## ref

**Symbol:** `ref` (defined as `compile/ref`)

**Syntax:**
- Variable reference: `(ref SYM)`
- Attribute reference: `(ref (attr OBJ NAME))` or `(ref (compile/attr OBJ NAME))`
- Item reference: `(ref (item OBJ KEY))` or `(ref (compile/item OBJ KEY))`
- Parameter reference: `(ref (parameter NAME))` → `(ParamRef 'NAME)`

**Use-case:** User-facing syntax for obtaining a mutable reference. The `&expr`
reader macro expands to `(compile/ref expr)`.

**Behaviour:** Expands via syntax-rules. Plain symbol expands to
`(compile/refq SYM)`. `(attr OBJ NAME)` and `(compile/attr OBJ NAME)` expand to
`(compile/ref-attr OBJ NAME)`. `(item OBJ KEY)` and `(compile/item OBJ KEY)`
expand to `(compile/ref-item OBJ KEY)`. `(parameter NAME)` expands to
`(ParamRef 'NAME)` (symbol `NAME` is quoted in the expansion).

**Examples:**

```scheme
(ref x)                    ; → (compile/refq x)
(ref (attr obj count))     ; → (compile/ref-attr obj count)
(ref (item lst 0))         ; → (compile/ref-item lst 0)
&foo                       ; reader: (compile/ref foo) → refq
&foo.bar                   ; reader: (compile/ref (attr foo bar)) → ref-attr
```

**Defined in:** `koskari/importer/basics.ki`


## compile/if

**Symbol:** `compile/if`

**Syntax:** `(compile/if COND THEN ELSE)` or `(compile/if COND then: THEN else: ELSE)` or `(compile/if COND THEN)`

**Use-case:** Binary conditional. Expands to `compile/cond`.

**Behaviour:** Macro in basics.ki. Expands to
`(compile/cond [COND THEN] [else: ELSE])` (or `[else: None]` when no else).

**Examples:**

```scheme
(compile/if True 1 2)    ; → 1
(compile/if False 1 2)   ; → 2
```

**Defined in:** `koskari/importer/basics.ki`


## compile/when

**Symbol:** `compile/when`

**Syntax:** `(compile/when COND THEN)`

**Use-case:** Conditional with no else; yields `None` when condition is false.
Expands to `compile/cond`.

**Behaviour:** Macro in basics.ki. Expands to
`(compile/cond [COND THEN] [else: None])` (single consequent form `THEN`).

**Examples:**

```scheme
(compile/when True 42)   ; → 42
(compile/when False 42)  ; → None
```

**Defined in:** `koskari/importer/basics.ki`


## compile/unless

**Symbol:** `compile/unless`

**Syntax:** `(compile/unless COND THEN)`

**Use-case:** Execute `THEN` when condition is false; when true, yields `None`.
Expands to `compile/cond` (negated test).

**Behaviour:** Macro in basics.ki. Expands to
`(compile/cond [COND None] [else: THEN])` (single consequent form `THEN`).

**Examples:**

```scheme
(compile/unless False 42)  ; → 42
(compile/unless True 42)   ; → None
```

**Defined in:** `koskari/importer/basics.ki`

**Tests:** `tests/importer/test_basics.py` — `TestConditionalMacros`


## compile/bind!

**Symbol:** `compile/bind!`

**Syntax:**

- `(bind! NAME VALUE)` — plain value
- `(bind! NAME (item OBJ KEY))` or `(bind! NAME (compile/item OBJ KEY))`
- `(bind! NAME (attr OBJ NAME))` or `(bind! NAME (compile/attr OBJ NAME))`
- `(bind! NAME (dynamic r ...))`
- `(bind! NAME (constant VALUE))`
- `(bind! NAME (memoized GETFN))`
- `(bind! NAME (parameter PARAM-NAME r ...))` — `ParamRef` with quoted
  `PARAM-NAME` and optional tail arguments (e.g. default); there is no
  separate `parameterq` pattern here

**Use-case:** User-facing binding syntax. Expands to `compile/bindq` with the
appropriate Ref type for item/attr/dynamic/constant/memoized/parameter forms.

**Defined in:** `koskari/importer/basics.ki`


## compile/define-parameter

**Symbol:** `compile/define-parameter`

**Syntax:**
- `(define-parameter NAME)` — bind `NAME` to `(ParamRef 'NAME)`
- `(define-parameter NAME DEFAULT)` — bind `NAME` to `(ParamRef 'NAME DEFAULT)`

**Use-case:** Define a name and bind it to a `ParamRef` for `parameterize`
scope, optionally with a default.

**Behaviour:** Expands to `(compile/define NAME)` and
`(compile/bindq NAME (ParamRef 'NAME ...))` (with `DEFAULT` when present).

**Defined in:** `koskari/importer/basics.ki`


## compile/parameterq

**Symbol:** `compile/parameterq`

**Syntax:**
- `(parameterq NAME)` — `(parameter 'NAME)`
- `(parameterq NAME DEFAULT)` — `(parameter 'NAME DEFAULT)`

**Use-case:** Construct a dynamic-parameter object with a quoted name symbol,
optionally with a default value.

**Behaviour:** Expands to `(parameter 'NAME)` or `(parameter 'NAME DEFAULT)`.

**Defined in:** `koskari/importer/basics.ki`


## compile/None?

**Symbol:** `compile/None?`

**Syntax:** `(None? VALUE)`

**Use-case:** Test whether a value is `None` at compile time via `compile/is`.

**Behaviour:** Expands to `(compile/is None VALUE)`.

**Defined in:** `koskari/importer/basics.ki`


## None?

**Symbol:** `None?` (function, not only the macro pattern above)

**Syntax:** `(None? VALUE)` in expression position uses the macro; the prelude
also defines a callable:

`(define None? [value]: (compile/None? value))`

**Use-case:** Predicate usable as a function value or in macro-expanded calls.

**Defined in:** `koskari/importer/basics.ki`


## define-coroutine

**Symbol:** `define-coroutine`

**Syntax:** `(define-coroutine NAME ARGS BODY ...)`

**Use-case:** Define `NAME` as a Python coroutine function for interop (e.g.
`inspect.iscoroutinefunction`), by wrapping a Koskari lambda with `as-coroutine`
from bootstrap.

**Behaviour:** Expands to
`(define NAME (as-coroutine (lambda NAME ARGS BODY ...)))`.

**Defined in:** `koskari/importer/basics.ki`


## when-main

**Symbol:** `when-main`

**Syntax:** `(when-main BODY ...)`

**Use-case:** Run `BODY ...` only when the module is executed as `__main__`
(typical entry-point guard).

**Behaviour:** Expands to

`when (eq? __name__ "__main__"): BODY ...`

**Defined in:** `koskari/importer/basics.ki`
