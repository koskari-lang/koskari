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
**`syntax-match`**, **`macro-match`**, **`special-rules`**, or **`special-match`**.
The compiler then lowers those **`syntax-rules` / `macro-rules` / …** specs via
the specials **`compile/syntax-rules`**, **`compile/macro-rules`**,
**`compile/syntax-match`**, **`compile/macro-match`**, **`compile/special-rules`**,
and **`compile/special-match`** (see [SPECIALS.md](SPECIALS.md)), and binds the
result with **`compile/define`**.

**Design (living):** [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md) — matcher IR, **RULE** vs **MATCH** pattern languages (**`syntax-rules`** / **`macro-rules`** vs **`syntax-match`** / **`macro-match`**), post-expand hygiene and **`Syntax`** boxes, and how `define-syntax` transformers align with the same ideas as Python specials. Reader macros, hygiene, and compiler optimizations around macros: [SPECIALS.md](SPECIALS.md) (§ Macros, reader macros, hygiene, and compiler optimizations); [language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md).

Macros documented here are defined in `koskari/importer/basics.ki` and are
available in every module without import. The format mirrors SPECIALS.md to
ease future migration of purely transformative forms from specials to macros.


## compile/define-syntax (macro)

**Symbol:** `compile/define-syntax` — the macro that defines other macros.

**Syntax:** The macro is defined in `basics.ki` and matches **`(define-syntax …)`**
forms whose transformer is one of:

- `(syntax-rules literals rule …)` → `(compile/define name (syntax-rules name literals rule …))`
- `(syntax-match (PATTERN TEMPLATE) …)` → `(compile/define name (syntax-match name (PATTERN TEMPLATE) …))`
- `(macro-rules literals rule …)` → `(compile/define name (macro-rules name literals rule …))`
- `(macro-match (PATTERN BODY …) …)` → `(compile/define name (macro-match name (PATTERN BODY …) …))`
- `(special-rules literals rule …)` → `(compile/define name (special-rules name literals rule …))`
- `(special-match rule …)` → `(compile/define name (special-match name rule …))`

**Use-case:** Install a compile-time transformer under the given name. After
expansion, the **value** of **`compile/define`** is built from the **`syntax-rules`** /
**`macro-rules`** / **`special-rules`** / … spec (see specials above).

**Behaviour:** Purely macro expansion; no dedicated `@special` handler for
`compile/define-syntax` in the compiler.

**Defined in:** `koskari/importer/basics.ki`

**Tests:** `tests/importer/test_basics.py` (where applicable); compile specials
for **`compile/syntax-rules`** / **`compile/macro-rules`** / **`compile/special-rules`**
/ **`compile/special-match`**: `tests/compile/specials/test_define_syntax.py`


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


## first, second, third, fourth, fifth; cadr, caddr, cadddr, caddddr

**Symbols:**

- **Positional:** `first`, `second`, `third`, `fourth`, `fifth` (defined as
  `compile/first` … `compile/fifth`)
- **`c*r`:** `cadr`, `caddr`, `cadddr`, `caddddr` (defined as `compile/cadr` …)

**Syntax:** `(first pair)` … `(fifth pair)`; `(cadr pair)` … `(caddddr pair)` — one
argument each (the list or pair chain).

**Use-case:** Take the first through fifth elements of a list (1-based) without
hand-writing nested `car` / `cdr`. The `c*r` names match usual Lisp/Scheme usage:
`cadr` is the 2nd element, … `caddddr` is the 5th.

**Behaviour:** In `basics.ki`, `compile/first` is bound to `compile/car`;
`compile/second` … `compile/fifth` are bound to `compile/cadr` …
`compile/caddddr`. The `compile/cadr` family are `define-syntax` /
`syntax-rules` macros that expand to nested `(car (cdr …))` (as many `cdr` hops
as there are `d` letters between `ca` and `r` in the name).

**Examples:**

```scheme
(first '(a b c))     ; → a   (same as car)
(second '(a b c))    ; → b
(caddr '(a b c d))   ; → c
(fifth '(a b c d e)) ; → e
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


## compile/nil?

**Symbol:** `compile/nil?`

**Syntax:** `(nil? VALUE)`

**Use-case:** Test whether a value is `nil` via `compile/is`.

**Behaviour:** Expands to `(compile/is nil VALUE)`.

**Defined in:** `koskari/importer/basics.ki`


## compile/each

**Symbol:** `compile/each`

**Syntax:**

- `(each [BINDING SEQ] when: CONDITION BODY)` — yield `BODY` only when `CONDITION`
- `(each [BINDING SEQ] unless: CONDITION BODY)` — yield `BODY` only when `CONDITION` is false
- `(each [BINDING SEQ] BODY)` — yield `BODY` for every iteration

**Use-case:** Generator comprehension sugar: expands to a thunked **`for-each`**
that **`yield`**s each body result (optionally filtered with **`when:`** /
**`unless:`**).

**Behaviour:** Wraps **`for-each`** in **`lambda`** and uses **`when`** /
**`unless`** for the filtered variants, as in `basics.ki`.

**Defined in:** `koskari/importer/basics.ki`


## compile/list-each

**Symbol:** `compile/list-each`

**Syntax:** `(list-each [BINDING SEQ] BODY ...)`

**Use-case:** Collect **`compile/each`** results into a list.

**Behaviour:** Expands to `(list (each [BINDING SEQ] BODY ...))`.

**Defined in:** `koskari/importer/basics.ki`


## compile/dict-each

**Symbol:** `compile/dict-each`

**Syntax:**

- `(dict-each [BINDING SEQ] key: KEY value: VALUE)` — build dict from **`cons`** pairs
- Variants with **`when:`** / **`unless:`** filters
- `(dict-each [BINDING SEQ] BODY ...)` — **`BODY`** should produce pairs

**Use-case:** Dict comprehension over a sequence via **`compile/each`**.

**Defined in:** `koskari/importer/basics.ki`


## compile/define-demanding

**Symbol:** `compile/define-demanding`

**Syntax:** `(define-demanding NAME FORMALS BODY ...)`

**Use-case:** Define **`NAME`** as a **`demand`**-wrapped lambda (lazy / on-demand
callable).

**Behaviour:** Expands to `(define NAME (demand (lambda NAME FORMALS BODY ...)))`.

**Defined in:** `koskari/importer/basics.ki`


## compile/define-class

**Symbol:** `compile/define-class`

**Syntax:** `(define-class NAME SUPERS BODY ...)`

**Use-case:** Define **NAME** as a class: expands to **`define`** around a **`class`**
form so the class object is stored in **NAME** (the **`compile/class`** special
alone only produces a value; it does not bind **NAME** — see
[SPECIALS.md](SPECIALS.md) § compile/class).

**Behaviour:** Expands to **`(define NAME (class NAME SUPERS BODY ...))`**. The
inner **`class`** form is the same shape the reader uses for **`layout/class`**
(lowered to **`compile/class`**). Block syntax uses **`layout/define-class`**;
see [LAYOUTS.md](LAYOUTS.md).

**Defined in:** `koskari/importer/basics.ki`


## compile/decorate

**Symbol:** `compile/decorate`

**Syntax:**

- `(decorate [] FN)` — **FN** unchanged
- `(decorate [DECORATOR ...] (define-class NAME SUPERS BODY ...))` — class with
  decorators on the **class** expression inside **`define`**
- `(decorate [DECORATOR ...] (define NAME FORMALS BODY ...))` — function define
  with decorators on the **`lambda`**
- `(decorate [DECORATOR ...] FN)` — non-define **FN**: **`((function-compose DECORATOR ...) FN)`**

**Use-case:** Apply one or more decorators to a **`define-class`**, a function
**`define`**, or an arbitrary callable expression.

**Behaviour:** Syntax-rules in **`basics.ki`**. For **`define-class`** and
**`define`**, rewrites so **`decorate`** wraps the **`class`** or **`lambda`**
value before **`define`** stores it. For a bare **FN**, composes decorators
with **`function-compose`** (see **`compile/function-compose`** in the same file).

**Defined in:** `koskari/importer/basics.ki`


## compile/function-compose

**Symbol:** `compile/function-compose`

**Syntax:** `(function-compose FN)` or `(function-compose FN1 FN2 ...)`

**Use-case:** Right-fold decorator composition: the composed callable accepts
**`*args`** and **`**kwds`** and threads calls through **FN1**, **FN2**, …

**Behaviour:** Macro-rules: single **FN** is unchanged; multiple **FN**s build a
**`lambda [*: args **: kwds]`** that nests calls inner-to-outer.

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
