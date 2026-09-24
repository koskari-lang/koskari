# Koskari Macros — Implemented Reference

This document describes the macros currently implemented in Koskari. Macros
expand at compile time before the compiler sees the result; the expanded forms
are then compiled as normal. For compiler special forms, see
[SPECIALS.md](SPECIALS.md).

Bootstrap loads these sources in order (`koskari/bootstrap/__init__.py`,
`_BUILTINS_SOURCES`):

1. `_define_syntax.ki` — meta-macros for `define-syntax` / `extend-syntax` / `let-syntax`
2. `_conditionals.ki` — `if`, `when`, `unless`, `?`, `bool`, `None?`, `nil?`
3. `_iter.ki` — `each`, `list-each`, `dict-each`, `has?`, slice helpers
4. `_variables.ki` — assignment, refs, bindings, comprehensions helpers
5. `_help.ki` — help formatting helpers
6. `_pairs.ki` — pair utilities
7. `_functions.ki` — coroutine/demand defines, decorators, composition
8. `_class.ki` — classes, dataclasses, `super`
9. `_pragma.ki` — `pragma` set/read forms, `(help pragma)`
10. `_assert.ki` — `assert`, gated by the `compiler/enable-asserts` pragma
11. `_control.ki` — bare forms for the statement words, gated by
    `compiler/bare-control-words`
12. `_fences.ki` — `define-fence`, `fence/bash`
13. `basics.ki` — list accessors, `help`, macro expansion, compile/evaluate helpers,
    `inline-macro`

The merged module is exposed as `koskari.builtins`. Macros are available in every
module without import.

User-facing **`define-syntax`** is not a compiler special: it is expanded by
the **`compile/define-syntax`** macro in `_define_syntax.ki`. The compiler
lowers transformer specs via **`compile/syntax-rules`**, **`compile/macro-rules`**,
and related specials (see [SPECIALS.md](SPECIALS.md) § compile/define-syntax).

**Binding keys are injected.** A transformer written as `NAME` is bound under
`compile/NAME` whichever spelling the definition used; resolution is
prefix-only (the short name is the use, the prefixed spelling denotes the
transformer object), and the written name stays the transformer's *identity*
for help text, match errors, and pattern literals. The injection is
idempotent, computed by a helper hidden in the `let` that defines the two
meta-macros.

### Expansion in operator position

Two expansion paths exist beyond the ordinary symbol-headed macro use, both
implemented in `Compiler` (`compile_symbol`, `compile_pair`, `lower_head`):

- **Bare forms.** A `Syntax` may carry one `BareTransform` in its dedicated
  `bare` slot (the `bare-syntax` transformer kind): a *lone symbol* whose
  `compile/NAME` binding has a bare arm expands before the runtime slot is
  ever consulted. A **bare-only** syntax (no call arms) used in form
  position is an **alias**: the head expands through the bare arm and the
  tail rides along for recompilation. A syntax *with* call arms never falls
  back to its bare arm — a form no call arm matches raises
  `SyntaxMatchError`. Bare arms are template-only; the called-by symbol is
  the sole capture, carried with the caller-threaded source position.

- **Pair heads** (`Compiler.lower_head`). When a form's head is itself a
  form, the compiler attempts to lower it: attempts nest through arbitrary
  depth, and only a head whose innermost head names a syntax is attempted
  at all. An expansion yielding *source* succeeds — each enclosing level is
  reconstructed around it (positions filled) and the whole form re-enters
  compilation from the top, so an expansion landing on a symbol becomes a
  fresh syntax use with the tail as its arguments. A non-syntax head, or an
  expansion that compiles to bytecode, fails the entire attempt with
  nothing committed (the scratch fragment is never merged), and the raw
  head flows to `compile/call` untouched — preserving the `(attr …)` head
  patterns and their `CALL_ATTR` fusion. Failure semantics are
  all-or-nothing per attempt; successes restart, so no partial rewrites
  ever mix with a failure.

  Interlocking rule: **`begin` of a single form is that form** — the
  `compile/begin` special returns a one-form body for recompilation, and
  the layout rule lowering only wraps *multi-form* bodies in `begin` to
  start with. Without this, every layout-defined macro's template would
  arrive wrapped, and the wrapper would defeat head lowering and the
  call-attr routing behind it.

  Known cost knob (unmeasured, deliberate): an attempted head that
  compiles to bytecode — every `(attr obj name)` method-call head — is
  compiled twice at compile time, once discarded. A statically decidable
  fast path exists (SpecialTransform-matched arms always fragment) but
  would demote form-returning specials in head position; it is deferred
  until measurement warrants it. There is no cycle guard on the
  expansion-restart loop beyond `full_compile`'s existing exposure — see
  TODO.md § macro-expansion cycle checks.

**Design:** [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md),
[MATCHER.md](MATCHER.md), [archive/macro-hygiene-syntax-boxes.md](archive/macro-hygiene-syntax-boxes.md).
Local compile-time binding: [archive/let-syntax-and-inline-macro.md](archive/let-syntax-and-inline-macro.md) (archived).

### Block quasiquote and `:block:` layout expansion

Block backtick reads as **`(compile/quasiquote-block (:block: …))`**, rewritten to
**`(compile/quasiquote (:block: …))`**. `define-syntax` output is finalized through
**`koskari.syntax._finalize_macro_output`**, which calls **`apply_layout`** and
then peels the `Datum` boxes. A template arm's symbols are marked during
transcription and carry their marks into layout, which reads block heads
through the base; a procedural arm's output is marked after layout (see
[SPECIALS.md](SPECIALS.md) § compile/quasiquote-block).


## koskari/bootstrap/_define_syntax.ki

### compile/define-syntax (macro)

**Symbol:** `compile/define-syntax`

**Syntax:** Matches **`(define-syntax NAME TRANSFORMER)`** where **`TRANSFORMER`** is
one of the seven kinds — `syntax-rules`, `syntax-match`, `macro-rules`,
`macro-match`, `special-rules`, `special-match`, or `bare-syntax`.

**Behaviour:** a **`macro-rules`** core (not templates: each arm computes the
binding key). Every kind arm expands to
**`(define-global compile/NAME (KIND NAME …))`** — the binding key carries the
`compile/` prefix (idempotently injected), while the *written* name is passed
to the builder as the transformer's identity. `bare-syntax` takes a called-by
slot and a template and produces a transformer whose lone-symbol form expands
(see *Expansion in operator position* above). The docstring arm —
`(define-syntax NAME DOCSTR (KIND …))` — is **not** in the core: it arrives as
an `extend-syntax` extension later in the same `let`, re-expanding the plain
definition and appending the docstring via `Syntax.append_doc`. The core, the
extend-syntax core, and both docstring extensions live in one `let` closing
over the binding-key helper; the two meta-macro cores are written as raw
builder calls because a definer cannot be used in the compile unit that
defines it.

**Tests:** `tests/compile/specials/test_define_syntax.py`,
`tests/importer/test_bare_syntax.py`


### compile/extend-syntax

**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same seven transformer
kinds as `define-syntax`, plus the docstring arm.

**Behaviour:** Rebinds **`compile/NAME`** via **`setq`** with
**`(+ compile/NAME (KIND NAME …))`** — the combination prepends the new arms
so they win where both match, the written name stays the identity, and the
original documentation carries forward (a docstring given in the extension is
*appended* through `Syntax.append_doc`, not replaced). A syntax carries at
most one bare arm, so extending with `bare-syntax` replaces any existing bare
arm.


### compile/let-syntax

**Symbol:** `compile/let-syntax` (surface: `let-syntax`)

**Syntax:** **`(let-syntax NAME (syntax-rules literals rule …) body …)`**

**Behaviour:** **`special-rules`** compile special (not a source rewrite macro). At
expand time it builds the same **`(syntax-rules NAME literals rule …)`** form
**`define-syntax`** would compile, runs **`full_compile`** on that spec and evaluates
the resulting expression to obtain a **`Syntax`** transformer. The body is compiled
inside **`Compiler.macro_overlay`**, which pushes the transformer onto a stack consulted
by **`find_compiled`** before the module namespace; the overlay key is the
**`compile/NAME`** binding spelling, so prefix-only resolution finds the local
syntax through the same probe as a global one. Nested **`let-syntax`** shadows
outer bindings. The overlay is discarded when the body compile finishes; nothing is
stored in the module namespace.

**Note:** Current bootstrap arm is **`syntax-rules`** only (unary binding).

**Tests:** `tests/importer/test_let_syntax.py`


### compile/datum

**Symbol:** `compile/datum`

**Syntax:** Used as `(datum expr)` or `(datum expr pos)` inside macro bodies.

**Behaviour:** **`macro-rules`** expansion to **`(call datum expr [position])`**, capturing
source position and marking the value as use-site material, which hygiene
does not mark. In a template `datum` is an ordinary call.


### compile/datumq

**Symbol:** `compile/datumq`

**Syntax:** `(datumq form)` or `(datumq form pos)`; reader shorthand `-'form`.

**Behaviour:** In code, a **`macro-rules`** expansion to
**`(call datum (quote form) [position])`**: the form itself, unevaluated, as
use-site material. In a **`syntax-rules`** or **`syntax-match`** template,
`(datumq form)` is a directive the transcriber consumes: it emits a `Datum` of
the raw form, unmarked and unsubstituted, with the position of the spine cell
holding the form, and it takes exactly one form. A template writes a name it
shares with the use site this way; `((datumq datumq) ...)` emits a literal
call, and `-'-'form` reaches a nested template as `(datumq form)`. See
[MATCHER.md](MATCHER.md) under Raw forms in templates.


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


### compile/?

**Symbol:** `compile/?` (surface: `?`)

**Syntax:** `(? COND THEN ELSE)`, `(? COND THEN)` (else **`False`**), or `(? COND)` (boolean test only).

**Behaviour:** Two- and three-argument forms expand to **`compile/cond`** with an **`else:`** arm. The unary form expands to **`(compile/truth COND)`**.


### compile/bool

**Syntax:** `(bool VALUE)`

**Behaviour:** **`(compile/truth VALUE)`** — Koskari truthiness as a boolean.


### compile/when-main

**Symbol:** `compile/when-main` (surface: `when-main`)

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to **`when (equal? __name__ "__main__"): (begin BODY ...)`**.


### compile/None? and compile/nil?

**Syntax:** `(None? VALUE)`, `(nil? VALUE)`

**Behaviour:** Identity predicates for the **`None`** and **`nil`** singletons. Expand to
**`(compile/is VALUE None)`** and **`(compile/is VALUE nil)`**.


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


### compile/flet

**Symbol:** `flet`

**Syntax:** `(flet [[NAME FORMALS BODY ...] ...] FORM ...)`

**Behaviour:** Binds local functions for the extent of a body. Each declaration
expands to a **`let`** binding whose value is an anonymous **`lambda`**:

```text
(flet [[double [x] (* x 2)]
       [inc [x] (+ x 1)]]
  (double (inc 20)))
  ⇒ (let [[double (lambda [x] (* x 2))]
          [inc (lambda [x] (+ x 1))]]
       (double (inc 20)))
```

Because **`let`** initializers are evaluated in the enclosing scope, a binding
shadows an outer function of the same name but does **not** see its siblings in
the same **`flet`** (no mutual recursion). Use **`labels`** when the functions
must call one another.


### compile/labels

**Symbol:** `labels`

**Syntax:** `(labels [[NAME FORMALS BODY ...] ...] FORM ...)`

**Behaviour:** Like **`flet`**, but the bound names are visible to one another so
the functions can be mutually recursive. Each declaration is **`define`**d inside
a shared (empty) **`let`** scope, so sibling references resolve there before the
body runs:

```text
(labels [[even? [n] (if (== n 0) True (odd? (- n 1)))]
         [odd? [n] (if (== n 0) False (even? (- n 1)))]]
  (even? 10))
  ⇒ (let []
       (define even? (function even? [n] (if (== n 0) True (odd? (- n 1)))))
       (define odd? (function odd? [n] (if (== n 0) False (even? (- n 1)))))
       (even? 10))
```

Both **`flet`** and **`labels`** capture each declaration's body under two
ellipsis levels (**`BODY ...`** nested inside the declaration-list **`...`**), so
they rely on depth-aware nested-ellipsis expansion in **`syntax-rules`**
templates (see [archive/labels-and-flet.md](archive/labels-and-flet.md)).

As with any local binding, a **`labels`** binding whose name is also a **macro**
(a special form, or a **`cad*r`** accessor) does not take effect in call position:
macros expand before names resolve, so they always win. Runtime function and
module bindings are shadowed normally — this is general to all bindings, not
specific to **`labels`**.


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

**Behaviour:** Expands to **`setq`**, **`compile/setq-values`**, **`compile/inplace`**, **`compile/set-item`**, **`compile/set-attr`**, or **`compile/set-ref`**. Augmented assignment on a symbol **`(set! name (OP name value))`** expands to **`compile/inplace`**.


### compile/inplace (extend-syntax)

**Symbol:** `compile/inplace` (extended in `_variables.ki`)

**Syntax:** **`(inplace OP (item OBJ KEY) VALUE)`**; **`(inplace OP (compile/item OBJ KEY) VALUE)`**;
**`(inplace OP (attr OBJ NAME) VALUE)`**; **`(inplace OP (compile/attr OBJ NAME) VALUE)`**.

**Behaviour:** Dispatches item and attribute places to the compiler specials
**`compile/inplace-item`** and **`compile/inplace-attr`**. Symbol bindings are
not handled here; those use the base **`compile/inplace`** special (via **`set!`**
augmented forms or direct **`(compile/inplace OP VAR VALUE)`**).

**Examples:**

```scheme
(inplace + (item vec i) 1)
(inplace * (attr obj scale) 2)
```


### compile/del!

**Symbol:** `del!`

**Syntax:** **`(del! name)`**, **`(del! (item obj key))`**, **`(del! (attr obj name))`**.

**Behaviour:** **`delq`**, **`del-item`**, **`del-attr`**.


### compile/take!

**Symbol:** `take!`

**Syntax:** **`(take! name)`**, **`(take! (item obj key))`**, **`(take! (attr obj name))`**
(and **`compile/item`**, **`compile/attr`** literal arms).

**Behaviour:** **`takeq`**, **`take-item`**, **`take-attr`**.


### compile/incr! and compile/decr!

**Syntax:** **`(incr! NAME [VALUE])`**, **`(decr! NAME [VALUE])`** (default step `1`).

**Behaviour:** **`compile/inplace`** with **`+`** or **`-`**.


### compile/ref

**Syntax:** **`(ref SYM)`**; **`(ref (item …))`**; **`(ref (attr …))`**; **`(ref (parameter NAME))`**;
**`(ref (environ KEY [DEFAULT]))`**.

**Behaviour:** **`compile/refq`**, **`compile/ref-item`**, **`compile/ref-attr`**, **`(ParamRef 'NAME)`**,
**`(environ-ref 'KEY [DEFAULT])`**.


### compile/bind!

**Syntax:** Value binding; **`(bind! name (item …))`** / **`(attr …)`**; **`(dynamic r …)`**;
**`(constant value)`**; **`(memoized getfn)`**; **`(parameter param-name r …)`**;
**`(environ key [default])`**.

**Behaviour:** **`compile/bindq`** with **`Ref`** types as appropriate.


### compile/global and compile/local

**Syntax:** **`(global name)`** → **`(compile/define-global name)`**;
**`(local name)`** / **`(local name value)`** → **`compile/define`**.


### compile/define-parameter and compile/parameterq

**Syntax:** **`(define-parameter NAME [DEFAULT])`**, **`(parameterq NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`ParamRef`** binding; **`parameterq`** → **`parameter`**.


### compile/define-environ

**Syntax:** **`(define-environ NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`environ-ref`** binding — declares **`NAME`** as a
local bound to the process environment variable named **`NAME`**. Without a
**`DEFAULT`** the ref reads/writes **`os.environ[NAME]`** directly (a missing
variable raises on deref); with a **`DEFAULT`** the ref returns it when the
variable is unset. See **`environ-ref`**.


### environ-ref

**Syntax:** **`(environ-ref KEY [DEFAULT])`**.

**Behaviour:** Builds a **`Ref`** into **`os.environ`** for string key **`KEY`**.
Without **`DEFAULT`** returns an **`ItemRef`** into **`os.environ`**; with a
**`DEFAULT`** returns a **`DynamicRef`** whose deref falls back to **`DEFAULT`**
via **`environ.get`**, whose store sets **`os.environ[KEY]`**, and whose clear
deletes it. An empty **`KEY`** raises **`ValueError`**.


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

**Behaviour:** **`macro-rules`** → **`(define-global __features__ #(…))`**
with sorted, deduplicated string names. Each name is a **feature stem**: at
end of load, every loader runs `koskari.module.materialize_exports`, which
expands the stems against the settled namespace into `__all__` — the binding
keys that actually exist (runtime name, `compile/` and `layout/` facets). A
name written with a facet prefix is a literal single-binding export. See
[api/import-system.md](api/import-system.md) § Module Lifecycle.


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
**`when`** / **`unless`**). A **BODY** that is a single **`(when …)`** /
**`(unless …)`** form is a whole-body filter, rewriting to the matching
clause — same contract and caveats as `compile/list-each` below. Note
`when` and `unless` must be pattern **literals** in the rules; as capture
variables the filter case false-matches any single-form call body.


### compile/list-each

**Syntax:** **`(list-each [BINDING SEQ] BODY)`** (with optional **`when:`** /
**`unless:`** clauses).

**Behaviour:** **`define col (#list)`**; **`for-each`** with **`#list-append`**; return **`(takeq col)`** (clears gensym accumulator slot).

**Whole-body filter:** a **BODY** that is a single **`(when …)`** or
**`(unless …)`** form is a filter — it rewrites to the matching
**`when:`** / **`unless:`** clause, so the collection happens inside the
conditional and skipped elements contribute nothing (no `None` gaps).
This applies only when the conditional encompasses the whole body: a
multi-form body keeps plain value semantics (a trailing `when` collects
its `None` on a miss), and filters do not compose — a nested `when` is
an ordinary expression, so write **`(when (and A B) …)`** for a
composite check, or **`(if RULE X None)`** to collect the `None`
deliberately.


### compile/dict-each

**Syntax:** **`(dict-each [BINDING SEQ] key: KEY value: VALUE)`** (with optional **`when:`** /
**`unless:`**); or **`(dict-each [BINDING SEQ] BODY …)`** for pair-producing bodies.

**Behaviour:** **`dict`** over **`compile/each`** results.


### compile/has?

**Symbol:** `compile/has?` (surface: `has?`)

**Syntax:** `(has? COLLECTION ELEMENT)`

**Behaviour:** **`(compile/in ELEMENT COLLECTION)`** — true when **COLLECTION** contains
**ELEMENT**.


### build-slice

Not a macro: a **`define`** function in `_iter.ki` that constructs a Python
**`slice`** from keyword arguments **`start:`**, **`stop:`**, and **`step:`** (each
defaults to **`None`**). **`(build-slice)`** → **`(slice None None None)`**.


### compile/slice

**Symbol:** `compile/slice` (surface: `slice`)

**Syntax:** **`(slice)`**, **`(slice ARGS …)`**

**Behaviour:** Adapter that routes slice construction to **`build-slice`** so keyword
arguments work. **`(slice)`** → **`(call build-slice)`**; **`(slice ARGS …)`** →
**`(call build-slice ARGS …)`**.


### compile/item-slice

**Symbol:** `compile/item-slice` (surface: `item-slice`)

**Syntax:** **`(item-slice SEQUENCE)`**, **`(item-slice SEQUENCE ARGS …)`**

**Behaviour:** Sequence slicing via **`item`**. **`(item-slice SEQUENCE)`** →
**`(item SEQUENCE (call build-slice))`**. With slice arguments, equivalent to
**`(item SEQUENCE (slice ARGS …))`**.


## koskari/bootstrap/_pragma.ki


### compile/pragma

**Symbol:** `compile/pragma` (surface: `pragma`)

**Syntax:**
- **`(pragma [NS/KEY VALUE …] …)`** — *set*: each bracketed form is one setting,
  a slash-namespaced key followed by positional and/or keyword values (a bare
  **`[NS/KEY]`** applies the setting's parameter defaults).
- **`(pragma NS/KEY)`** — *read* the current value of one setting.
- **`(pragma)`** — the module's **`ModulePragma`** object.

**Behaviour:** The set form lowers each setting to a
**`#('NS/KEY (values VALUE …))`** tuple and expands to
**`(setq __pragma__ (.rebuild __pragma__ TUPLE …))`** — rebuilding the immutable
**`ModulePragma`** and rebinding **`__pragma__`**, which announces the change to
the subscribed reader and compiler (forward-only, module-scoped). Because each
`VALUE` lands in a `values` call it is evaluated, so a literal symbol must be
quoted (**`'error`**) while booleans/numbers/strings are bare.
**`(pragma NS/KEY)`** expands to **`(item __pragma__ "NS/KEY")`**; **`(pragma)`**
expands to **`__pragma__`**. See [PRAGMA.md](PRAGMA.md) for the available
settings.


### active-pragma (function + macro)

**Symbol:** `compile/active-pragma` (surface: `active-pragma`), with a
same-named runtime function

**Syntax:** **`(active-pragma)`**, **`(active-pragma NS/KEY)`**

**Behaviour:** Reads the pragma the compiler bound for the **current
compilation turn** (`Compiler.compile_expression` binds `self.pragma` into
the `active-compiler-pragma` parameter around each turn). Inside a macro
body — a closure over its *defining* module — this answers for the module
*being compiled*, where `(pragma …)` would answer for the wrong module.
When no pragma is bound, a lazily-built default `ModulePragma` carrying
every registered default answers instead, so gates behave sanely under a
bare `Compiler`. The macro quotes the key and calls the function; the
function and its cache live in a `let`.


### (help pragma)

**Syntax:** **`(help pragma)`**, **`(help (pragma NS/KEY))`**

**Behaviour:** **`extend-syntax`** cases on **`compile/help`**.
**`(help pragma)`** diverts to the module's live **`__pragma__`** and renders
**`ModulePragma.__kos_help__`** — the current settings plus every registered
setting and its default. **`(help (pragma NS/KEY))`** renders help for one
setting: the signature and docstring of its registered function.


## koskari/bootstrap/_assert.ki

### compile/assert

**Symbol:** `compile/assert` (surface: `assert`)

**Syntax:** **`(assert EXPR)`**, **`(assert EXPR MESSAGE)`**

**Behaviour:** **`macro-rules`** expansion gated at expand time by
**`(active-pragma compiler/enable-asserts)`** — the pragma of the module
*being compiled*, not the module the macro was defined in, so each module
answers for its own setting and modules never cross-contaminate. Enabled,
expands to **`(unless EXPR (raise (AssertionError MESSAGE?)))`** — so
**`MESSAGE`** is evaluated only on failure. Disabled, expands to
**`(begin)`**: neither **`EXPR`** nor **`MESSAGE`** is compiled or evaluated
(Python `-O` semantics). Because the gate is consulted per expansion, a
pragma change is forward-only — an assert compiled while disabled stays
compiled away even if asserts are re-enabled before it runs.

The setting function registered for `compiler/enable-asserts` is a pure
coercer; no shared gate state exists.

**Tests:** `tests/importer/test_assert.py`


## koskari/bootstrap/_control.ki

### compile/bare-control-word

**Symbol:** `compile/bare-control-word`

**Syntax:** `(bare-control-word WORD)` — emitted by the statement words'
bare forms, not written by hand.

**Behaviour:** **`special-rules`** gate consulted when a bare statement
word converts. Reads **`(active-pragma compiler/bare-control-words)`**:
`convert` (baseline) expands silently to `(WORD)`, `warn` emits a
`SyntaxWarning` through `warn_explicit` with the koskari filename and the
captured source line, `error` raises a `CompilerError` carrying position
and filename. Diagnostics carry the position threaded through the bare
capture.


### bare forms: return, continue, break, raise, yield, pass

**Behaviour:** `extend-syntax … bare-syntax` blocks give each statement
word a bare form expanding through the gate, so the lone symbol becomes
its call form — `raise` alone is Python's re-raise spelling, and `pass`
expands to `(begin)`. The
deliberate cost: these six names always expand and can never name a
variable.

**Tests:** `tests/importer/test_bare_syntax.py`,
`tests/importer/test_active_pragma.py`


## koskari/bootstrap/basics.ki

### compile/cadr, caddr, cadddr, caddddr (and cddr, cdddr, cddddr)

**Syntax:** **`(cadr pair)`**, etc.

**Behaviour:** Expand to nested **`car`** / **`cdr`** (not **`compile/`**-prefixed heads in
the **`syntax-rules`** patterns). Each accessor also has a runtime **function** of the
same name, so the names work both in call position (macro-expanded) and as
first-class values.

**`first`** … **`fifth`** are runtime function aliases only (**`first`** = **`car`**,
**`second`** = **`cadr`**, … **`fifth`** = **`caddddr`**) — no longer macros, so they can
be shadowed by a local binding where the macro accessors cannot.


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


### compile/inline-macro

**Symbol:** `compile/inline-macro` (surface: `inline-macro`)

**Syntax:** **`(inline-macro body …)`**

**Behaviour:** **`special-rules`** compile special. At expand time the body forms are
unwrapped from **`Datum`**, combined into **`(begin body …)`**, compiled and evaluated
once via **`full_compile`** and **`as_expression`**. The resulting form is returned
for the compiler to compile in place of the **`inline-macro`** site (compile-time
source splice). No transformer is installed and nothing is bound in the module
namespace.

**Tests:** `tests/importer/test_let_syntax.py`
