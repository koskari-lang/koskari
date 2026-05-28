# Koskari Macros — Implemented Reference

This document describes the macros currently implemented in Koskari. Macros
expand at compile time before the compiler sees the result; the expanded forms
are then compiled as normal. For compiler special forms, see
[SPECIALS.md](SPECIALS.md).

Bootstrap loads these sources in order (`koskari/bootstrap/__init__.py`,
`_BUILTINS_SOURCES`):

1. `_define_syntax.ki` — meta-macros for `define-syntax` / `extend-syntax`
2. `_conditionals.ki` — `if`, `when`, `unless`, predicates
3. `_functions.ki` — coroutine/demand defines, decorators, composition
4. `_variables.ki` — assignment, refs, bindings, comprehensions helpers
5. `_class.ki` — classes, dataclasses, `super`
6. `_iter.ki` — `each`, `list-each`, `dict-each`
7. `basics.ki` — list accessors, `help`, macro expansion, compile/evaluate helpers

The merged module is exposed as `koskari.builtins`. Macros are available in every
module without import.

User-facing **`define-syntax`** is not a compiler special: it is expanded by
the **`compile/define-syntax`** macro in `_define_syntax.ki`. The compiler
lowers transformer specs via **`compile/syntax-rules`**, **`compile/macro-rules`**,
and related specials (see [SPECIALS.md](SPECIALS.md) § compile/define-syntax).

**Design:** [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md),
[MATCHER.md](MATCHER.md), [language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md).

### Block quasiquote and `:block:` layout expansion

Block backtick reads as **`(compile/quasiquote-block (:block: …))`**, rewritten to
**`(compile/quasiquote (:block: …))`**. `define-syntax` output is finalized through
**`koskari.syntax._finalize_macro_output`**, which calls **`apply_layout`** before
hygiene (see [SPECIALS.md](SPECIALS.md) § compile/quasiquote-block).


## koskari/bootstrap/_define_syntax.ki

### compile/define-syntax (macro)

**Symbol:** `compile/define-syntax`

**Syntax:** Matches **`(define-syntax NAME TRANSFORMER)`** where **`TRANSFORMER`** is:

- `(syntax-rules literals rule …)` → `(compile/define name (syntax-rules name literals rule …))`
- `(syntax-match rule …)` → `(compile/define name (syntax-match name rule …))`
- `(macro-rules literals rule …)` → `(compile/define name (macro-rules name literals rule …))`
- `(macro-match rule …)` → `(compile/define name (macro-match name rule …))`
- `(special-rules literals rule …)` → `(compile/define name (special-rules name literals rule …))`
- `(special-match rule …)` → `(compile/define name (special-match name rule …))`
- `(define-syntax name docstr (form …))` — re-expands with docstring attached via `set-attr` on `__doc__`

**Layout:** `define layout/define-syntax layout/flat` (flat block read).

**Tests:** `tests/compile/specials/test_define_syntax.py`


### compile/extend-syntax

**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same six transformer heads as
`define-syntax`.

**Behaviour:** Replaces **`NAME`** via **`setq`** with a new **`syntax-rules`** /
**`syntax-match`** / **`macro-rules`** / **`special-rules`** / **`special-match`**
spec (each expansion passes **`NAME`** as the first argument to the builder form).

**Layout:** `define layout/extend-syntax layout/flat`.


### compile/datum

**Symbol:** `compile/datum`

**Syntax:** Used as `(datum expr)` or `(datum expr pos)` inside macro bodies.

**Behaviour:** **`macro-rules`** expansion to **`(call datum expr [position])`**, capturing
source position for hygiene.


## koskari/bootstrap/_conditionals.ki

### compile/if

**Syntax:** `(if COND THEN ELSE)`, `(if COND then: THEN else: ELSE)`, or `(if COND THEN)` (else `None`).

**Behaviour:** Expands to **`compile/cond`** with **`else:`** clause.


### compile/when

**Syntax:** `(when COND THEN)`

**Behaviour:** **`(compile/cond [COND THEN] [else: None])`**.


### compile/unless

**Syntax:** `(unless COND THEN)`

**Behaviour:** **`(compile/cond [COND None] [else: THEN])`**.


### compile/when-main

**Symbol:** `compile/when-main` (surface: `when-main`)

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to **`when (equal? __name__ "__main__"): (begin BODY ...)`**.


### compile/None? and compile/nil?

**Syntax:** `(None? VALUE)`, `(nil? VALUE)`

**Behaviour:** **`(compile/is None VALUE)`** and **`(compile/is nil VALUE)`**.


### on-demand?

Not a macro: a **`define`** function in `_conditionals.ki` that probes whether the
current evaluation path forbids **`await`** (catches **`KoskariSyncViolation`** from
bare **`(await)`**).


## koskari/bootstrap/_functions.ki

### compile/define-coroutine

**Symbol:** `define-coroutine`

**Syntax:** `(define-coroutine NAME ARGS BODY ...)`

**Behaviour:** **`(define NAME (coroutine-fn (lambda NAME ARGS BODY ...)))`**.


### compile/define-demanding

**Syntax:** `(define-demanding NAME FORMALS BODY ...)`

**Behaviour:** **`(define NAME (demand (lambda NAME FORMALS BODY ...)))`**.


### compile/function-compose

**Syntax:** `(function-compose FN)` or `(function-compose FN1 FN2 …)`

**Behaviour:** **`macro-rules`** expansion. A single **FN** is unchanged. Two or more
**FN**s expand to a **`lambda [*: args **: kwds]`** that nests calls from right to left:
the rightmost **FN** receives **`*: args **: kwds`**, and each **FN** to the left wraps
the accumulated call (same order as **`(decorate [DECORATOR …] FN)`** folding). Example
from the macro docstring:

```text
(function-compose fn1 (fn2 foo) fn3)
  ⇒ (lambda [*: args **: kwds]
       (fn1 ((fn2 foo) (fn3 *: args **: kwds))))
```

Composition is syntactic: argument expressions are not evaluated until the composed
function is invoked.


### compile/decorate

**Syntax:**

- `(decorate [] FN)`
- `(decorate [DECORATOR ...] (define-class NAME SUPERS BODY ...))`
- `(decorate [DECORATOR ...] (define NAME FORMALS BODY ...))`
- `(decorate [DECORATOR ...] FN)` — **`((function-compose DECORATOR ...) FN)`**

**Behaviour:** Wraps **`class`** / **`lambda`** inside **`define`** / **`define-class`**, or
composes decorators for a bare callable.


## koskari/bootstrap/_variables.ki

### compile/set!

**Symbol:** `set!` (`compile/set!`)

**Syntax:** Plain **`(set! SYM EXPR)`**; **`(set! (values PATTERN ...) VALUE)`**;
augmented **`(set! SYM (OP SYM VAL))`**; **`(set! (item …) VAL)`**; **`(set! (attr …) VAL)`**;
**`(set! (deref REF) VAL)`** (and **`compile/item`**, **`compile/attr`**, **`compile/deref`**
variants).

**Behaviour:** Expands to **`setq`**, **`compile/setq-values`**, **`compile/inplace`**, **`compile/set-item`**, **`compile/set-attr`**, or **`compile/set-ref`**.


### compile/del!

**Symbol:** `del!`

**Syntax:** **`(del! name)`**, **`(del! (item obj key))`**, **`(del! (attr obj name))`**.

**Behaviour:** **`delq`**, **`del-item`**, **`del-attr`**.


### compile/incr! and compile/decr!

**Syntax:** **`(incr! NAME [VALUE])`**, **`(decr! NAME [VALUE])`** (default step `1`).

**Behaviour:** **`compile/inplace`** with **`+`** or **`-`**.


### compile/ref

**Syntax:** **`(ref SYM)`**; **`(ref (item …))`**; **`(ref (attr …))`**; **`(ref (parameter NAME))`**.

**Behaviour:** **`compile/refq`**, **`compile/ref-item`**, **`compile/ref-attr`**, **`(ParamRef 'NAME)`**.


### compile/bind!

**Syntax:** Value binding; **`(bind! name (item …))`** / **`(attr …)`**; **`(dynamic r …)`**;
**`(constant value)`**; **`(memoized getfn)`**; **`(parameter param-name r …)`**.

**Behaviour:** **`compile/bindq`** with **`Ref`** types as appropriate.


### compile/global and compile/local

**Syntax:** **`(global name)`** → **`(compile/define-global name)`**;
**`(local name)`** / **`(local name value)`** → **`compile/define`**.


### compile/define-parameter and compile/parameterq

**Syntax:** **`(define-parameter NAME [DEFAULT])`**, **`(parameterq NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`ParamRef`** binding; **`parameterq`** → **`parameter`**.


### compile/let-values

**Syntax:** **`(let-values [PATTERN VALUE] body …)`**

**Behaviour:** **`(let [[resolved VALUE]] (compile/define-values PATTERN resolved) body …)`**.


### compile/let*

**Syntax:** **`(let* [[NAME INIT] …] body …)`**

**Behaviour:** Nested **`let`** (empty, single, or recursive **`(let [first] (let* [rest …] body …))`**).


### compile/item* and compile/attr*

**Syntax:** **`(item* OBJ KEY …)`**, **`(attr* OBJ NAME …)`**

**Behaviour:** Left-nested **`item`** / **`attr`** chains.


### compile/declare-exports

**Syntax:** **`(declare-exports name …)`**

**Behaviour:** **`macro-rules`** → **`(define-global __all__ #(…))`** with sorted, deduplicated string names.


## koskari/bootstrap/_class.ki

### compile/define-class

**Syntax:** **`(define-class NAME SUPERS BODY …)`**

**Behaviour:** **`(define NAME (class NAME SUPERS BODY …))`** plus **`create-instance-check`**
(defines **`NAME?`** as **`lambda [value] (compile/instance? value NAME)`**).


### compile/define-dataclass

**Syntax:** **`(define-dataclass NAME (members …))`** or with **`SUPERS`**.

**Behaviour:** Imports **`dataclass`** / **`field`**, emits **`@dataclass class`**, then
**`create-instance-check`** for **`NAME?`**.


### compile/super

**Syntax:** **`(super)`**, **`(super ARGS …)`**

**Behaviour:** **`(super)`** → **`(compile/call super (type self) self)`**;
**`(super ARGS …)`** → **`(compile/call super ARGS …)`**.


## koskari/bootstrap/_iter.ki

### compile/each

**Syntax:** **`(each [BINDING SEQ] BODY)`**; with **`when:`** or **`unless:`** filter.

**Behaviour:** **`lambda each`** wrapping **`for-each`** + **`yield`** (filtered variants use
**`when`** / **`unless`**).


### compile/list-each

**Syntax:** **`(list-each [BINDING SEQ] BODY)`

**Behaviour:** **`letrec`** with **`list`** + **`append`**; **`for-each`** body; returns the list.


### compile/dict-each

**Syntax:** **`(dict-each [BINDING SEQ] key: KEY value: VALUE)`** (with optional **`when:`** /
**`unless:`**); or **`(dict-each [BINDING SEQ] BODY …)`** for pair-producing bodies.

**Behaviour:** **`dict`** over **`compile/each`** results.


## koskari/bootstrap/basics.ki

### compile/cadr, caddr, cadddr, caddddr

**Syntax:** **`(cadr pair)`**, etc.

**Behaviour:** Expand to nested **`car`** / **`cdr`** (not **`compile/`**-prefixed heads in
the **`syntax-rules`** patterns).


### first … fifth

**Not macros:** **`define`** aliases — **`compile/first`** = **`compile/car`**;
**`compile/second`** … **`compile/fifth`** = **`compile/cadr`** … **`compile/caddddr`**.


### compile/help

**Syntax:** **`(help)`**, **`(help obj)`**, **`(help obj ns)`**

**Behaviour:** **`(compile/call help …)`** — no namespace: **`(help 'help (globals))`**;
object only: **`(help 'obj (globals))`**; object and namespace passed through.


### Runtime macro expansion (not macros)

**Symbols:** **`macroexpand-1`**, **`macroexpand`** (`define-global` in the same file).

**Behaviour:** Look up **`__compiler__`** on the namespace, **`find_compiled`** on the form
head, match a **`Syntax`** transformer, and run **`handle_expand`** once
(**`macroexpand-1`**) or in a loop until no macro applies (**`macroexpand`**). If the
result is a single-expression **`begin`** / **`compile/begin`**, that wrapper is stripped.


### compile/macroexpand-1

**Syntax:** **`(macroexpand-1 form)`**, **`(macroexpand-1 form ns)`**

**Behaviour:** **`(call macroexpand-1 form (globals))`** or **`(call macroexpand-1 form ns)`**.


### compile/macroexpand

**Syntax:** **`(macroexpand form)`**, **`(macroexpand form ns)`**

**Behaviour:** **`(call macroexpand form (globals))`** or **`(call macroexpand form ns)`**.


### compile/macroexpandq-1

**Syntax:** **`(macroexpandq-1 form)`**, **`(macroexpandq-1 form ns)`**

**Behaviour:** **`(call macroexpand-1 (quote form) (globals))`** or
**`(call macroexpand-1 (quote form) ns)`**.


### compile/macroexpandq

**Syntax:** **`(macroexpandq form)`**, **`(macroexpandq form ns)`**

**Behaviour:** **`(call macroexpand (quote form) (globals))`** or
**`(call macroexpand (quote form) ns)`**.


### Runtime compile and evaluate (not macros)

**Symbols:** **`compile`**, **`evaluate`** (`define-global`).

**Behaviour:** **`compile`** resolves **`__compiler__`** from **`ns`**, runs
**`full_compile`**, returns an **`Expression`** (or passes through if already
**`Expression`** / **`Function`**). **`evaluate`** compiles when needed, then invokes
**`as_expression`**.


### compile/evaluate

**Syntax:** **`(evaluate form)`**, **`(evaluate form ns)`**

**Behaviour:** **`(call evaluate form (locals))`** or **`(call evaluate form ns)`**.


### compile/compileq

**Syntax:** **`(compileq form)`**, **`(compileq form ns)`**

**Behaviour:** **`(call compile (quote form) (locals))`** or **`(call compile (quote form) ns)`**.
