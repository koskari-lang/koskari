# Koskari Special Forms — Implemented Reference

This document describes the special forms currently implemented in the Koskari
compiler. It is the reference for use-cases, syntax, and examples when adding or
changing specials. For a broader catalog of planned forms (from python-sibilant
and sibilant-ceval), see [language/special-forms.md](language/special-forms.md).

Specials are registered in the compiler's module namespace; the compiler
dispatches on the head symbol of a list form. The forms below are implemented in
`koskari/compile/specials/` and use helpers from `koskari/compile/helpers.py`
where applicable. Many handlers declare structure with **`koskari.matcher`**
patterns (`@special(..., patterns=...)`); see [MATCHER.md](MATCHER.md) for
matcher syntax, capture shapes, and hygiene behaviour.

## Macros, reader macros, hygiene, and compiler optimizations

**Reader macros** — Parse-time character dispatch is implemented on
`koskari.reader.parser.Parser`: `set_macro_character`, `remove_macro_character`,
and default readers for quote, quasiquote, unquote, and unquote-splicing.

**Compile-time macros** — User-facing **`(define-syntax NAME SPEC)`** is expanded
by the **`compile/define-syntax`** macro in `koskari/bootstrap/_define_syntax.ki` (not a
`@special`). **`SPEC`** must begin with **`syntax-rules`**, **`macro-rules`**,
**`syntax-match`**, **`macro-match`**, **`special-rules`**, or **`special-match`**;
see **`compile/define-syntax`** and the **`compile/syntax-rules`**,
**`compile/macro-rules`**, **`compile/syntax-match`**, **`compile/macro-match`**,
**`compile/special-rules`**, and **`compile/special-match`** sections below.
**RULE** vs **MATCH** pattern languages and the shared transformer model are
documented in
[language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md).
Canonical matcher and hygiene reference:
[MATCHER.md](MATCHER.md).

**Special-form transformers** — **`special-rules`** and **`special-match`** build
**`SpecialRulesTransformer`** values (`koskari/syntax.py`): rule procedures are
invoked with **`compiler`**, **`tc`**, and keyword arguments from the match
(unlike **`macro-rules`** / **`macro-match`**, which receive only keyword args).
Expansion uses **`unbox_macro`** (not **`unbox_macro_with_hygiene`**).

**Syntax boxes and hygiene** — **`koskari.syntax`** and **`koskari.hygiene`** own syntax-rule expansion and template hygiene; macro output runs **`koskari.reader.layouts.apply_layout`** (module namespace) before binding-symbol discovery and the hygiene / Datum finalize passes; see
[language/macro-hygiene-syntax-boxes.md](language/macro-hygiene-syntax-boxes.md),
[api/matcher-hygiene-extensions.md](api/matcher-hygiene-extensions.md), and
[MATCHER.md](MATCHER.md) ( **`Datum`**, **`DatumFrame`**, which matchers capture
and which register hygiene regions).


## compile/bytecode

**Symbol:** `compile/bytecode`

**Syntax:** `(compile/bytecode [OP ARG] ...)`

**Use-case:** Direct bytecode injection for tests, debugging, or hand-written
code. Each element of the body is a pair (or list) `[OP ARG]` where `OP` is an
opcode name (e.g. `CONST`, `RETURN`) or integer, and `ARG` is the instruction
argument (optional; defaults to 0).

**Behaviour:** Compiles to a single fragment that emits the given instructions in
order. No constant-pool or label resolution is done inside the form; the
compiler's normal `complete` pass handles that. Opcode names may be given with
or without the `KOS_OP_` prefix.

**Examples:**

```scheme
;; Return a constant string
(compile/bytecode
  [CONST "Hello World"]
  [RETURN 1])

;; Load two variables and call a function of two arguments
(compile/bytecode
  [VAR add]
  [VAR value_a]
  [VAR value_b]
  [CALL 2]
  [RETURN 1])
```

## compile/begin

**Symbol:** `compile/begin`

**Syntax:** `(compile/begin EXPR ...)`

**Use-case:** Sequential evaluation of expressions; useful for side effects and
for running several forms in order while returning only the last value. Common
building block for blocks and bodies.

**Behaviour:** Compiles each expression in order. All but the last expression are
evaluated and then popped from the stack (their values are discarded). The last
expression's value is left on the stack (and is the value of the begin). Empty
body compiles to pushing `None` and returning it.

**Examples:**

```scheme
;; Single expression: just that value
(compile/begin (compile/call print "hello"))

;; Multiple expressions: run all, value is the last
(compile/begin
  (compile/call do-something)
  (compile/call compute-result))
```

## compile/call

**Symbol:** `compile/call`

**Syntax:** `(compile/call CALLEE ARG ...)`

Argument specifiers (order matters; analogous to Python 3.5+):


| Form        | Syntax     | Example          | Pythonic equivalent |
| ----------- | ---------- | ---------------- | ------------------- |
| Positional  | `EXPR ...` | `(f a b c)`      | `f(a, b, c)`        |
| Keyword     | `KEY: VAL` | `(f x: 1 y: 2)`  | `f(x=1, y=2)`       |
| Star args   | `*: EXPR`  | `(f *: more)`    | `f(*more)`          |
| Star kwargs | `**: EXPR` | `(f **: kwargs)` | `f(**kwargs)`       |


Positionals may interleave with `*:` (star args); keywords may interleave with
`**:` (star kwargs). All positionals and `*:` must come before any keywords or
`**:`; a keyword cannot be followed by `*:` or a positional. Full form examples:

```scheme
(compile/call fn a b *: more x: xval y: yval **: kwargs)
```

is equivalent to `fn(a, b, *more, x=xval, y=yval, **kwargs)` in Python.

```scheme
(compile/call fn a *: bargs c *: dargs)
(compile/call fn x: 1 **: kw1 y: 2 **: kw2)
```

are equivalent to `fn(a, *bargs, c, *dargs)` and
`fn(x=1, **kw1, y=2, **kw2)` in Python.

**Use-case:** Generic function application. Evaluates the callee and each
argument, then emits CALL (positional only), CALL_NAMES (positional + keywords),
or CALL_STAR (any use of `*:` or `**:`).

**Behaviour:** If the callee is `(attr OBJ SYM)` or `(compile/attr OBJ SYM)`,
the form expands to `(compile/call-attr OBJ SYM ARGS...)` and that form is
compiled instead. If the callee is a symbol whose name begins with `.` and
there is at least one argument, the first argument is treated as the receiver
`OBJ` and the remainder as `ARGS...`; the leading dot is stripped from the
callee symbol to obtain `SYM`, and the form expands to
`(compile/call-attr OBJ SYM ARGS...)` the same way.

Otherwise, compiles the first subform (the callee), then parses the remaining
forms with `gather_parameters` into positionals and
star-args (interleaved) and keywords and starstar-args (interleaved).
Positional-only calls emit `CALL n`; keyword calls emit `CALL_NAMES`; any star
or starstar use emits `CALL_STAR`. Tail-call mode (`tc`) is respected when set
by the compiler.

**Examples:**

```scheme
;; Positional
(compile/call + 1 2)
(compile/call my-func a b c)

;; Keyword (KEY: VAL)
(compile/call fn 1 2 name: "x")

;; Star args (*: EXPR) and star kwargs (**: EXPR)
(compile/call apply-fn *: args)
(compile/call merge *: pos **: kw)
(compile/call fn a b *: rest x: 1 **: extra)

;; Interleaved (Python 3.5+ style)
(compile/call fn a *: bargs c *: dargs)
(compile/call fn x: 1 **: kw1 y: 2 **: kw2)

;; Method call via attr expansion: (compile/call (attr obj m) args) → compile/call-attr
(compile/call (attr s upper))
(compile/call (compile/attr obj split sep) "-")

;; Dot-prefix callee: (compile/call .m obj args...) → compile/call-attr (same as attr)
(compile/call .upper s)
(compile/call .split s "-")
```

## compile/call-attr

**Symbol:** `compile/call-attr`

**Syntax:** `(compile/call-attr OBJ SYM [ARG ...])`

**Use-case:** Call a method by name: resolve `obj.sym`, then invoke with the
given arguments. Equivalent to `obj.sym(arg...)` in Python. Supports positional
args, keyword args (`KEY: VAL`), and `*:` / `**:` (emits CALL_ATTR_STAR).

**Behaviour:** Compiles OBJ, then parses ARGS with `gather_parameters`.
Positional-only emits `CALL_ATTR`; positional + keywords emits `CALL_ATTR_NAMES`;
any `*:` or `**:` emits `CALL_ATTR_STAR`. SYM must be a symbol (the attribute
name).

**Note:** `(compile/call (attr obj name) args)` and `(compile/call (compile/attr obj name) args)`
expand to this form before compilation. So does `(compile/call .name obj args...)` when `.name`
is a symbol whose text starts with `.` and at least `obj` is present (the dotted callee is not
used as a variable).

**Examples:**

```scheme
;; Positional only
(compile/call-attr s upper)
(compile/call-attr s split sep)
(compile/call-attr s replace "l" "L")

;; With keywords
(compile/call-attr obj m 99 name: 42)

;; Star args (emits CALL_ATTR_STAR)
(compile/call-attr obj fn *: args **: kwargs)
```

**Tests:** `tests/compile/specials/test_call_attr.py`

## compile/lambda

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


`RET` is a single expression (not a formal parameter). Like parameter
annotations, it is compiled and evaluated in the **defining scope** at
function-creation time and becomes `__annotations__['return']` as a real
object (e.g. `:: int` yields the `int` type). A string literal is the
forward-reference escape hatch: it stays a string, resolved lazily (dotted
lookup against the defining namespace) on first `__annotations__` /
`__signature__` access; failures stay strings and are never retried. See
[archive/function-annotations-opcode.md](archive/function-annotations-opcode.md).

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b c: default-c]`, or a single symbol
for rest. Supports `*` and `**` in formals (see `gather_formals` in helpers and
the **FORMALS** composite in [MATCHER.md](MATCHER.md)).
Parameter types may use `::` / `type:` on individual parameters (see formals
helpers).

A positional or keyword parameter may be marked **by-ref** with a leading `&`
(e.g. `[a &result]`). The parser records it in the `by_refs` tuple on the
parsed `Formals`; at application the binder installs the caller-supplied `Ref`
as the slot's storage so `set!` in the callee writes through to the caller. Not
permitted on `*` / `**`. See [SYNTAX.md](SYNTAX.md) (By-ref formals).

**Docstring:** If the first form in `BODY` is a string literal, it is taken as
the function docstring (`discover_docstring` in helpers). It is attached to the
fragment, stored on `Code` as `code.extras['doc']`, and appears on the callable
as `__doc__`. A leading `(compile/begin ...)` in the body is peeled only for
docstring discovery; the body is still compiled in full.

**Use-case:** Create an anonymous or named function value. Named lambdas bind the
given name in the function's scope so the function can refer to itself (e.g. for
recursion in an immediately-invoked named `compile/let` or inline `(compile/lambda NAME ...)`).

**See also:** `compile/function` — named function values **without** a closure
self-binding at `BUILD_FUNCTION` time. Prefer `compile/function` for
`compile/define` / `compile/define-global` function shorthand and decorator
wrappers; prefer named `compile/lambda` when the body must call the exact
function object being created (e.g. named `compile/let`).

**Behaviour:** Matches one of the shapes above, parses formals with
`gather_formals`, builds a fragment that declares the parameter names, compiles
the body with `helper_begin`, and completes it as a function. Annotation
expressions (parameter types and `RET`) are compiled in the enclosing scope
and their values pushed first (string literals stay `CONST` forward
references), followed by keyword-default values, also compiled in the
enclosing scope. Pushes a `FunctionSpec` constant and emits `BUILD_FUNCTION`
with the defaults and annotation name tuples in its extra row (see
[OPCODES.md §16](OPCODES.md#kos_op_build_function)).

**Examples:**

```scheme
;; Anonymous identity
(compile/lambda [x] x)

;; Anonymous two-argument function
(compile/lambda [a b] (compile/call + a b))

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

**Tests:** `tests/compile/specials/test_lambda.py`

## compile/function

**Symbol:** `compile/function`

**Syntax** (named only; most specific forms first):


| Shape                    | Form                                                  |
| ------------------------ | ----------------------------------------------------- |
| Named + return           | `(compile/function NAME :: RET FORMALS BODY ...)`     |
| Named + return (`type:`) | `(compile/function NAME type: RET FORMALS BODY ...)` |
| Named, no return         | `(compile/function NAME FORMALS BODY ...)`            |


There is no anonymous `compile/function` form; use `compile/lambda` for that.

**Formals, return type, and docstring:** Same rules as `compile/lambda` (see
that section). `RET` is evaluated in the defining scope at function-creation
time and exposed as `__annotations__['return']`. A leading string in `BODY`
becomes `__doc__`.

**Use-case:** Create a **named** function value whose body must **not** receive
a closure self-binding for `NAME` at `BUILD_FUNCTION` time. The name in the
body resolves via outer or module bindings instead — e.g. after decorators wrap
the stored definition, or when `compile/define` / `compile/define-global`
function shorthand stores the function and recursive calls use that binding.

**Behaviour:** Same pipeline as `compile/lambda` via `helper_function` with
`selfref=False`: `gather_formals`, `helper_begin`, `FunctionSpec`,
`BUILD_FUNCTION`. Does **not** call `declare_var` for `NAME` on the inner
function fragment (contrast named `compile/lambda`).

**Examples:**

```scheme
;; Named function (no inner self-ref for double)
(compile/function double [x] (compile/call * x 2))

;; Return type
(compile/function f :: int [x] x)

;; Docstring + return type
(compile/function g :: int [x] "doc for g" x)
```

**Implementation:** `koskari/compile/specials/lambda_.py`

**Tests:** `tests/compile/specials/test_lambda.py` (shared helper);
`tests/compile/specials/test_define.py` (define / define-global shorthand)

## compile/let

**Symbol:** `compile/let`

**Syntax:**

- Unnamed: `(compile/let BINDINGS BODY ...)`
- Named: `(compile/let NAME BINDINGS BODY ...)`

**Bindings:** A list of binding forms; each is `[SYM EXPR]` or `(EXPR as: SYM)`.
Parsed with `gather_bindings` (see helpers). Matcher shape: **BINDINGS** with
`scope: body` in [MATCHER.md](MATCHER.md).

**Use-case:** Lexical bindings for the duration of the body. Unnamed let pushes a
child namespace (PUSH_NS), binds names to compiled values, runs the body, and
pops (POP_NS). Named let expands to an immediately-invoked named lambda:
`((lambda NAME [args...] BODY...) val...)`, so the name is bound in the
function's scope for recursion.

**Behaviour:** Unnamed: `child_namespace` with the binding names and value
expressions; body compiled with `helper_begin`. Named: rewrite to
`(compile/call (compile/lambda NAME [args...] BODY...) val...)` via `pycons`.

**Examples:**

```scheme
;; Unnamed let
(compile/let [[x 1] [y 2]] (compile/call + x y))

;; Named let (e.g. loop)
(compile/let loop [[n 10]]
  (compile/call loop ...))
```

## compile/cond

**Symbol:** `compile/cond`

**Syntax:** `(compile/cond [TEST BODY...] [TEST BODY...] ... [else: BODY...])`

**Use-case:** Multi-branch conditional. Evaluates clauses in order; the first
truthful test's body is evaluated and returned. No match yields `None`. The
`else:` keyword as test is always true (catch-all).

**Behaviour:** For each clause `(test body)`, emits LABEL, compiles test,
JUMP_FALSE to next clause, compiles body with `helper_begin`, JUMP to done.
If test is `else:`, compiles body and breaks. If no else clause, emits CONST
None for the fall-through case.

**Examples:**

```scheme
;; Empty cond → None
(compile/cond)

;; Single clause
(compile/cond [True 100])

;; else-only
(compile/cond [else: 100])

;; Multi-clause with else fallthrough
(compile/cond
  [False 100]
  [True 101]
  [else: 102])
```

**Note:** The higher-level forms `compile/if`, `compile/when`, and
`compile/unless` are macros in `koskari/bootstrap/_conditionals.ki` (see [MACROS.md](MACROS.md)).

## compile/switch

**Symbol:** `compile/switch`

**Syntax:** `(compile/switch VALUE [COMPARE-LIST BODY...] ... [else: BODY...])`

**Use-case:** Multi-way equality branching against one discriminant value.
`VALUE` is evaluated once, then each clause compares one or more candidate values
from `COMPARE-LIST` against `VALUE` in order. On first match, that clause body
is evaluated and returned. If no clause matches, `else:` is used when present;
otherwise the form yields `None`.

**Behaviour:** Requires at least one argument (`VALUE`). Compiles `VALUE` once
and keeps it on the stack across case checks. For each non-else clause, the
clause test must be a pair/list (`COMPARE-LIST`); otherwise `SpecialError` is
raised (`"switch compare must be a list of values"`). For each element of that
list, the compiler emits one compare path against `VALUE`:

- match (`SWITCH_EQ` not taken): emits `JUMP body_label`, then runs the clause
  body once
- no match (`SWITCH_EQ` taken): pops only candidate value, keeps `VALUE`, jumps
  to the next candidate (or next clause after the last candidate)

Clause bodies are compiled via `helper_begin`. The `else:` clause must be last.
Entering `else:` pops the retained `VALUE` before compiling its body. After any
successful clause, control jumps to a shared done label. If no clause matches
and no else is present, the remaining `VALUE` is popped and `None` is pushed.

The head may be written as `switch` or `compile/switch`; compiler lookup resolves
`switch` to `compile/switch`.

**Expected lowered shape from layout/switch:**

```scheme
(switch VALUE
  ((COND1 COND2 ...) (compile/begin BODY...))
  (else: (compile/begin BODY...)))
```

This shape is valid because each clause body is still a list of forms for
`helper_begin`; when it contains one `compile/begin` form, that form performs
the sequencing.

**Examples:**

```scheme
;; Direct s-expression form
(compile/switch 5
  [[1] "one"]
  [[2] "two"]
  [[5 6] "five-or-six"]
  [else: "meh"])
;; -> "five-or-six"

;; No match with else
(compile/switch 5
  [[1] "one"]
  [[2 3] "two-or-three"]
  [else: "meh"])
;; -> "meh"

;; No match and no else
(compile/switch 5
  [[1] "one"]
  [[2] "two"])
;; -> None
```

**Tests:** `tests/compile/specials/test_switch.py`,
`tests/reader/layouts/test_switch_layout.py`

## compile/and

**Symbol:** `compile/and`

**Syntax:** `(compile/and EXPR ...)`

**Use-case:** Short-circuit conjunction. Evaluates expressions left-to-right
and returns the first falsy value, skipping the rest. If all values are truthy,
returns the last value. Zero arguments yields `True` (the identity for and).
Mirrors Python's `and` operator.

**Behaviour:** Zero args: emits `CONST True`. One arg: compiles it directly.
Multiple args: compiles each expression in order; after each non-last expression,
emits `JUMP_AND done`. `JUMP_AND` pops TOS — if it is falsy, pushes it back and
jumps to `done` (short-circuit); if truthy, discards it and falls through so the
next expression can be evaluated. The last expression is compiled without a jump
and its value remains on the stack as the result. A `LABEL done` is appended.
No `POP` is needed between terms because `JUMP_AND` already consumes TOS on
the fall-through path.

**Opcodes emitted:** `CONST True` (zero args) or `JUMP_AND` (one per non-last
term).

**Examples:**

```scheme
(compile/and)             ; → True
(compile/and 42)          ; → 42
(compile/and 1 2 3)       ; → 3   (all truthy, last value)
(compile/and 1 None 3)    ; → None (short-circuit at second term)
(compile/and False 99)    ; → False
```

**Tests:** `tests/compile/specials/test_cond.py`

## compile/or

**Symbol:** `compile/or`

**Syntax:** `(compile/or EXPR ...)`

**Use-case:** Short-circuit disjunction. Evaluates expressions left-to-right
and returns the first truthy value, skipping the rest. If all values are falsy,
returns the last value. Zero arguments yields `False` (the identity for or).
Mirrors Python's `or` operator.

**Behaviour:** Zero args: emits `CONST False`. One arg: compiles it directly.
Multiple args: compiles each expression in order; after each non-last expression,
emits `JUMP_OR done`. `JUMP_OR` pops TOS — if it is truthy, pushes it back and
jumps to `done` (short-circuit); if falsy, discards it and falls through so the
next expression can be evaluated. The last expression is compiled without a jump
and its value remains on the stack as the result. A `LABEL done` is appended.
No `POP` is needed between terms because `JUMP_OR` already consumes TOS on
the fall-through path.

**Opcodes emitted:** `CONST False` (zero args) or `JUMP_OR` (one per non-last
term).

**Examples:**

```scheme
(compile/or)              ; → False
(compile/or 42)           ; → 42
(compile/or None False 0) ; → 0   (all falsy, last value)
(compile/or None 99 0)    ; → 99  (short-circuit at second term)
(compile/or False 42)     ; → 42
```

**Tests:** `tests/compile/specials/test_cond.py`

## compile/try

**Symbol:** `compile/try`

**Syntax:** `(try BODY CLAUSE...)`

where each `CLAUSE` is `(EXCEPTMATCH BODY...)` and `EXCEPTMATCH` is one of:

- `[TYPEEXPR]`, `[SYM TYPEEXPR]`, or `[TYPEEXPR as: SYM]` — USE form; type-only
or binding.
- `except:` — bare catch-all; catches any exception without a type check or binding

In addition, two keyword-head clauses are accepted:

- `(else: BODY...)` — run when the try body completed without exception
- `(finally: BODY...)` — always run; must be last

**Use-case:** Exception handling. Runs BODY; on exception, matches against
clauses in order and runs the first matching handler. Optional `else:` runs
when no exception occurred. Optional `finally:` always runs (on normal exit
or exception). At least one except-style clause or `finally:` is required.

**Behaviour:** Uses PUSH_TRY, CLEAR_TRY, POP_TRY, PUSH_FINALLY, POP_FINALLY.
Exception handlers use EXCEPTION_MATCH, EXCEPTION_CLEAR, EXCEPTION opcodes.
The bare `except:` form is lowered to an `else:` clause in the internal
`compile/cond` dispatch so no EXCEPTION_MATCH is emitted; uncaught exceptions
re-raise via compile/raise unless a bare handler is present. `else:` runs
between CLEAR_TRY and POP_TRY; `finally:` runs after cleanup.  Expressions in
`finally:` are compiled for side effects only; they do not replace the value
of the try form (including a pending `compile/return` from the try or except
paths).

**Examples:**

```scheme
;; Type-only catch
(try (risky-call)
  [[Exception] (handle-exception)])

;; Named binding
(try (parse input)
  [[e ValueError] (compile/call log e)])

;; Bare catch-all
(try (risky-call)
  [except: (handle-anything)])

;; Try/else/finally
(try (do-work)
  [else: (success)]
  [finally: (cleanup)])

;; Full form
(try (do-work)
  [[Exception] (handle-exc)]
  [else: (on-success)]
  [finally: (cleanup)])
```

**Tests:** `tests/compile/specials/test_try.py`

## compile/raise

**Symbol:** `compile/raise`

**Syntax:** `(compile/raise)` or `(compile/raise EXPR)`

**Use-case:** Re-raise the current exception (no arg) or raise a new exception
(EXPR). Used internally by try for re-raise on uncaught.

**Behaviour:** With no arg, emits RAISE 0 (re-raise). With EXPR, compiles EXPR,
emits RAISE 1 (raise the value as exception).

**Examples:**

```scheme
(compile/raise)
(compile/raise (ValueError "invalid"))
```

## compile/return

**Symbol:** `compile/return`

**Syntax:** `(compile/return VAL ...)` — variadic.

| Form                        | Result                                            |
|-----------------------------|---------------------------------------------------|
| `(compile/return)`          | returns `None`                                    |
| `(compile/return VAL)`      | returns `VAL`                                      |
| `(compile/return VAL ...)`  | returns a `tuple` of the values                    |
| `(compile/return K: V ...)` | returns a `values` object over the keyword pairs   |

**Use-case:** Return from the current function and transfer control back to the
caller. `return` is variadic: two or more positional arguments return a tuple,
and keyword arguments return a multi-value `values` object. Consume the result
with tuple indexing or with `compile/define-values` / `compile/let-values`.

**Behaviour:** Compiles each positional argument in turn and emits `RETURN N`,
where `N` is the argument count: `RETURN 0` returns `None`, `RETURN 1` returns
the single value, and `RETURN N` (for `N > 1`) leaves the values on the stack
for the opcode to package as a tuple. A keyword argument anywhere in the
argument list re-heads the form as `(values ...)` and returns its result, so
`(compile/return a: 1 b: 2)` compiles as `(compile/return (values a: 1 b: 2))`
and yields a `values` object; positional and keyword arguments must not be
mixed (the same rule `values` enforces).

At runtime, when the frame has a parent (e.g. normal CALL), RETURN walks the
dynamic chain with handle(RETURN), runs FINALLY if present, detaches the frame,
and delivers the value to the parent (opcode returns `EVAL_CONTROL_RETURNED()`
or `EVAL_NORMAL()`). When the frame has no parent or its parent is the generator
node (KOS_DYN_GEN), RETURN returns `EVAL_RETURN(val)`; `eval_event_return`
delivers and `koskari_eval` exits with `KOS_RES_RETURN`.

**Examples:**

```scheme
(compile/return)                  ;; -> None
(compile/return result)           ;; -> result
(compile/return a b c)            ;; -> #(a b c)  (a tuple)
(compile/return a: a b: b c: c)   ;; -> values(a=a, b=b, c=c)
```

## compile/check-exception

**Symbol:** `compile/check-exception`

**Syntax:** `(compile/check-exception TYPE ...)`

**Use-case:** Internal form used by compile/try. Checks if the current
exception matches any of the given types; if so, clears the exception and
continues; otherwise falls through.

**Behaviour:** Compiles each TYPE, emits EXCEPTION_MATCH with the count.
Used inside the cond built by _except.

**Examples:** Internal use only; not normally written by users.

## compile/clear-exception

**Symbol:** `compile/clear-exception`

**Syntax:** `(compile/clear-exception)`

**Use-case:** Internal form. Clears the current exception state.

**Behaviour:** Emits EXCEPTION_CLEAR. No arguments.

## compile/load-exception

**Symbol:** `compile/load-exception`

**Syntax:** `(compile/load-exception)`

**Use-case:** Internal form used by try handlers. Pushes the current exception
value onto the stack (e.g. for binding with as: NAME).

**Behaviour:** Emits EXCEPTION. No arguments.

## compile/while

**Symbol:** `compile/while`

**Syntax:** `(compile/while TEST BODY... [else: ELSEBODY])`

**Use-case:** Loop that runs body while TEST is truthy. Supports `compile/break`
and `compile/continue` inside the body. An optional `[else: ELSEBODY]` clause
runs only when the loop exits because TEST is false (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop evaluates to `None`. In block syntax, the same clause comes
from an `else:` peer; see `layout/while` in [LAYOUTS.md](LAYOUTS.md).

**Behaviour:** Emits PUSH_LOOP (break target after POP_LOOP). The continue
target is the first instruction after PUSH_LOOP. Compiles TEST; on false,
jumps to the loop exit label. Compiles the body with `helper_begin`, then
CONTINUE 1 back to the test. On normal exit (test false), when an else body is
present, emits CLEAR_LOOP, compiles ELSEBODY, then BREAK 1 so the else result
reaches the parent stack before POP_LOOP. User `compile/break` jumps to the
break target with its value and does not run the else clause.

**Examples:**

```scheme
;; Simple loop
(compile/while x
  (compile/call append c x)
  (compile/setq x (compile/call - x 1)))

;; With break
(compile/while True
  (compile/cond [done (compile/break result)]
                [else: (compile/call step)]))

;; Else runs when TEST becomes false
(compile/while x
  (compile/setq x (compile/call - x 1))
  [else: "done"])
```

**Tests:** `tests/compile/specials/test_while.py`

## compile/for-each

**Symbol:** `compile/for-each`

**Syntax:** `(compile/for-each [BINDING EXPR] BODY... [else: ELSEBODY])`

**Use-case:** Iterate over the sequence produced by EXPR, binding each element
to BINDING and evaluating BODY on each iteration. Supports `compile/break` and
`compile/continue` inside the body. An optional trailing **`[else: ELSEBODY]`**
keyword clause runs only when the iterator is exhausted naturally (i.e. not via
`compile/break`); its value becomes the loop's expression value. Without an
else clause the loop evaluates to `None`. In block syntax, the same clause comes
from an `else:` peer; see `layout/for-each` in [LAYOUTS.md](LAYOUTS.md).

**Behaviour:** Emits CONST None (default loop result), compiles EXPR (the
iterable), then PUSH_FOREACH whose break target is set to a label after
POP_FOREACH. The continue target (first instruction after PUSH_FOREACH) is
ITER_NEXT. On each iteration, ITER_NEXT pushes the next value or jumps to the
else label on exhaustion. The value is stored with VAR_STORE into the binding
name. The body is compiled with `helper_begin`; CONTINUE 0 resets the stack
block and jumps back to ITER_NEXT.

On exhaustion, CLEAR_FOREACH marks natural exit. When an else body is present, its
result is compiled and then BREAK 1 is emitted: this pops the FOREACH region,
pushes the else result directly to the parent stack, and jumps to after_pop —
so the else value arrives cleanly without any temporary variable. When there is
no else body, POP_FOREACH falls through to after_pop and pushes the region's None.
User `compile/break` also jumps to after_pop with its value already on the
parent stack.

**Examples:**

```scheme
;; Basic iteration; loop value is None
(compile/for-each [i items]
  (compile/call append result i))

;; Else fires when iterator exhausts without a break
(compile/for-each [i items]
  (compile/cond
    [(compile/call > i 4) (compile/break)]
    [else: (compile/call append result i)])
  [else: "finished"])

;; Break with a value
(compile/for-each [i items]
  (compile/cond
    [(compile/call eq i target) (compile/break "found")]
    [else: None]))
```

**Tests:** `tests/compile/specials/test_for_each.py`

## compile/globals

**Symbol:** `compile/globals`

**Syntax:** `(compile/globals)`

**Use-case:** Obtain the module globals namespace for the current frame:
walk `KosNamespace_PARENT` from the frame's namespace to the outermost parent
(analogous to Python's `globals()`). Useful for reflection, explicit binding
into the module namespace, or passing the globals dict to APIs that expect it.

**Behaviour:** Emits `GLOBALS` (argument 0). Pushes that namespace onto the
stack as the expression's value. Any arguments are rejected with `SpecialError`.

**Examples:**

```scheme
;; Module globals namespace for the current frame
(compile/globals)
```

**Implementation:** `koskari/compile/specials/variables.special_globals`

**Tests:** `tests/compile/specials/test_namespace_specials.py`

## compile/locals

**Symbol:** `compile/locals`

**Syntax:** `(compile/locals)`

**Use-case:** Obtain the current frame's namespace (locals), analogous to
Python's `locals()`.

**Behaviour:** Emits `LOCALS` (argument 0). Pushes the current frame's namespace
onto the stack. Any arguments are rejected with `SpecialError`.

**Examples:**

```scheme
(compile/locals)
```

**Implementation:** `koskari/compile/specials/variables.special_locals`

**Tests:** `tests/compile/specials/test_namespace_specials.py`

## compile/gensym-partial

**Symbol:** `compile/gensym-thunk`

**Syntax:** `(compile/gensym-thunk BODY ...)`

**Reader — thunk syntax:** In s-expression mode, `{BODY ...}` is **thunk syntax**
(also called **the thunk form**). It reads as `(compile/gensym-thunk BODY ...)`
— the same special with no extra macro. Nested `{...}` inside `BODY` read as
nested `(compile/gensym-thunk ...)`. The head `gensym-partial` also resolves
to this special via the usual `compile/` prefix. See
[archive/brace-lambdas-gensym-partial.md](archive/brace-lambdas-gensym-partial.md)
for the full language design.

**Use-case:** Short anonymous callables with inferred parameters: positional
placeholders `%1`, `%2`, … (contiguous from `%1`), optional rest from `...`
(Python `Ellipsis` at the call sites, lowered to `*` and a rest binding), and
zero-argument thunks when no placeholders appear. Handy for callbacks and
point-free-style partial application without writing `compile/lambda` formals by
hand.

**Behaviour:** Walks the body forms (rewrite everywhere; no quote boundary).
Symbols matching `%` + a positive integer (no leading zeros) are replaced by
fresh gensyms; repeated `%n` shares one binding. `...` is replaced by `*:`
list segments (`keyword("*")` and the rest gensym) for interop with
`compile/call`. Subtrees whose head is `gensym-thunk` or
`compile/gensym-thunk` are left unchanged (no recursion into the cdr).
After rewriting, placeholders must be contiguous `%1` … `%N`; otherwise
`SpecialError`. Builds inferred **formals** for `compile/lambda` (positionals in
order, then `*` and rest if `...` appeared) and returns the source form
`(compile/lambda FORMALS . BODY)` for the compiler to compile (same lowering
pattern as other macros that expand to a lambda).

**Examples:**

```scheme
;; Unary: { (+ %1 1) }  =>  thunk taking one argument
(compile/gensym-thunk (+ %1 1))

;; Thunk syntax in source
{ (print "hello") }

;; Thunk returning 99
{ 99 }

;; Variadic rest: ... in the body becomes * and a rest formal
(compile/gensym-thunk (f ...))
```

**Tests:** `tests/compile/specials/test_thunk.py`

## compile/break

**Symbol:** `compile/break`

**Syntax:** `(compile/break)` or `(compile/break VAL)`

**Use-case:** Exit the innermost loop (`compile/while` or `compile/for-each`),
optionally with a result value.

**Behaviour:** Compiles optional VAL (or pushes None), emits BREAK with count
0 or 1. Runtime unwinds to the loop's break target and pushes the result.

**Examples:**

```scheme
(compile/break)
(compile/break 99)
```

## compile/continue

**Symbol:** `compile/continue`

**Syntax:** `(compile/continue)` or `(compile/continue VAL)`

**Use-case:** Restart the innermost loop iteration. Must be inside a
`compile/while` or `compile/for-each` body.

**Behaviour:** Compiles optional VAL (or pushes None), emits CONTINUE with
count 0 or 1. Runtime jumps to the loop's continue target (loop head for while,
ITER_NEXT for for-each).

**Examples:**

```scheme
(compile/continue)
(compile/continue last-value)
```

**Tests:** `tests/compile/specials/test_cond.py`, `tests/compile/specials/test_while.py`

## compile/yield

**Symbol:** `compile/yield`

**Syntax:** `(compile/yield)` or `(compile/yield VAL)`

**Use-case:** Yield a value from a generator (or coroutine). Suspends the
current frame and returns the value to the driver; resumption is via
`send(value)` or `throw()`. With no argument, yields `None`.

**Behaviour:** With no arg, emits `YIELD 0` (yield None). With one argument,
compiles VAL and emits `YIELD 1`. At runtime, when the frame has a parent that
is **not** the generator node (e.g. a nested CALL inside a generator), YIELD
detaches the frame, pushes the value onto the parent stack, and returns 0 so
the loop continues in the caller. When the frame's parent is the generator node
(KOS_DYN_GEN) or there is no parent, the opcode returns `EVAL_YIELD(val)`;
`apply_kos_op_result` routes to `eval_event_yield`, which runs
`kos_dyn_walk_handle_yield`, the generator node consumes the yield (freezes the
dynamic chain and suspends), sets `SUSPENSION_EVENT = YIELD`, and `koskari_eval`
returns `KOS_RES_YIELD`. On resume, `revive_yield` pushes `send_val` onto the stack.

**Examples:**

```scheme
(compile/yield)
(compile/yield (compile/call compute-next))
```

## compile/yield-from

**Symbol:** `compile/yield-from`

**Syntax:** `(compile/yield-from EXPR)` — exactly one argument.

**Use-case:** Delegate to another iterable or async-iterable and yield each
produced value through the current async generator, analogous to Python's
`yield from`. Prefer this over `compile/for-each` plus `compile/yield` when you
only forward an entire sequence (for example `(compile/call items data)` for
`dict.items()`).

**Behaviour:** Compiles `EXPR` onto the stack, emits `YIELD_FROM` with operand
`0`, then emits `CONST None` so control can continue after delegation completes
(see `koskari/compile/specials/continuations.py`). At runtime the opcode
consumes the iterable value on TOS, classifies it (sync iterator or generator,
Python async iterator or async generator, Koskari async generator), rejects bare
coroutines, and drives the delegate until exhaustion while forwarding yields and
send/throw/close semantics. Like `compile/yield`, participating code is marked
with `KOS_CODE_YIELDS`. Full stack and suspension details are in
`design/OPCODES.md` (KOS_OP_YIELD_FROM) and `koskari/lib/opcode/YIELD_FROM.c`.

**Examples:**

```scheme
(compile/yield-from (compile/call items data))

;; Same consumer-visible values as:
(compile/for-each [pair (compile/call items data)]
  (compile/yield pair))
```

**Tests:** `tests/compile/specials/test_yield_from_special.py`,
`tests/lib/opcode/test_yield_from.py`

## compile/await

**Symbol:** `compile/await`

**Syntax:** `(compile/await)` or `(compile/await EXPR)` — zero or one argument.

**Use-case:** Koskari runs code under an async evaluator: ordinary Python calls
that return a coroutine or awaitable are **auto-awaited** at the CALL boundary,
so their final value is what you usually see on the stack. You rarely need an
explicit await for those paths. `compile/await` exists for values that are
**not** eagerly awaited—most importantly values produced by the **`defer`**
builtin (`defer(fn, *args, **kwds)` in Python; see `design/api/types.md` and
`design/api/defer-coroutine.md`). A defer is a single-shot await handle: the
wrapped call runs at **defer construction** (without auto-await on its result),
but the VM does **not** auto-await the defer itself, so you can pass the handle
around and choose **when** to await delegation or a cached sync result.
Use `(compile/await EXPR)` when `EXPR` evaluates to such a defer (or any other
awaitable you need to await explicitly at this program point).

With **no arguments**, `compile/await` emits a **bare** cooperative suspend
(`AWAIT` with stack count `0`) and then **`CONST None`** so the expression’s
value is `None`.

**Behaviour:** With one argument, compiles `EXPR` onto the stack, then emits
`AWAIT` with operand **1** (one stack element). The opcode pops TOS, dispatches
the awaitable, and may return `EVAL_AWAIT(val)` or push the resolved value.
With zero arguments, emits `AWAIT` with operand **0** then `CONST None`. See
`design/OPCODES.md` (KOS_OP_AWAIT) and `koskari/lib/opcode/AWAIT.c`.

**Examples:**

```scheme
;; Await a defer stored in d (defer was used so d is not auto-awaited)
(compile/await d)

;; Cooperative suspend only; value of the form is None
(compile/await)
```

**Tests:** `tests/compile/specials/test_await_special.py`

## compile/parameterize

**Symbol:** `compile/parameterize`

**Syntax:** `(compile/parameterize BINDINGS BODY ...)`

**Bindings:** Same as let: list of `[SYM EXPR]` or `(EXPR as: SYM)`; parsed with
`gather_bindings`.

**Use-case:** Temporarily rebind dynamic parameters for the duration of the
body. Parameters are consumed by functions that read them (e.g. via
ParamRef(name).deref(); bind-param not yet implemented). When the body runs,
calls to such functions see the parameterized values; when the block exits,
parameters are restored.

**Behaviour:** Compiles each value expression onto the stack (first binding
deepest, last at TOS), emits PUSH_PARAM with a constant names tuple, compiles the
body with `helper_begin`, then emits POP_PARAM. Uses the VM's PARAM extent;
restore on exit (and on unwind) is handled by the evaluator.

**Examples:**

```scheme
;; Parameterize format; consumer pretty reads it via ParamRef("format").deref()
(compile/parameterize [[format "woot %r woot"]]
  (compile/call pretty 100))

;; Multiple parameters
(compile/parameterize [[a 100] [b 101]]
  (compile/call check_both))
```

See `tests/compile/specials/test_parameterize.py` for full consumer-style tests.

## compile/dyn-wind

**Symbol:** `compile/dyn-wind`

**Syntax:**

- **Positional:** `(compile/dyn-wind BEFORE BODY AFTER)` — three elements in fixed order; `BEFORE` and/or `AFTER` may be `nil`.
- **Keyword:** `(compile/dyn-wind [before: BEFOREBODY...] [body: BODY...] [after: AFTERBODY...])` — top-level lists with marker keywords; omitted segments default to nil; order is arbitrary.

**Use-case:** Dynamic-wind extent: run before thunk on entry, body in the middle, after thunk on exit. Both thunks run on continuation transfer (multi-shot). Before and after are implicitly wrapped in null-arity lambdas; body is compiled as `(compile/begin ...)`.

**Behaviour:** The form accepts either positional or keyword syntax. Detection: if any top-level element is a pair whose `car` is keyword `before:`, `body:`, or `after:`, keyword mode is used; otherwise positional mode. In keyword mode, segments are identified by their marker; missing segments are nil. Body is required; at least one of before or after is required. Emits: push before thunk (or CONST nil), push after thunk (or CONST nil), PUSH_WIND (arg 1 or 2), compile body, POP_WIND. Stack layout for PUSH_WIND 2: `[..., before, after]` (before below, after above).

**Examples:**

```scheme
;; Positional: before, body, after
(compile/dyn-wind
  (compile/begin (compile/call setup))
  (compile/begin (compile/call work) result)
  (compile/begin (compile/call teardown)))

;; Positional: after only (before = nil)
(compile/dyn-wind nil
  (compile/begin 42)
  (compile/begin (compile/call cleanup)))

;; Keyword: before, body, after
(compile/dyn-wind
  (before: (compile/call setup))
  (body: (compile/call work) result)
  (after: (compile/call teardown)))

;; Keyword: body and after only (before omitted)
(compile/dyn-wind
  (body: 42)
  (after: (compile/call cleanup)))
```

**Tests:** `tests/compile/specials/test_dyn_wind.py`

## with

**Symbol:** `compile/with`

**Syntax:** `(with [name? expr] body ...)`

Optional `name` binds the context manager's enter result for the body. If
omitted, only `expr` and `body ...` are given (no binding).

**Use-case:** Single-shot context manager: evaluate `expr` to get a manager,
call enter (auto-await if needed), run body in a fresh operand stack block,
then call exit on normal or exception exit. Safe for normal control-flow and
exception unwind; **not** safe across continuation transfers (transfer across
an active `with` raises a runtime error).

**Behaviour:** Compiles `expr`, emits PUSH_WITH with optional name (constant
pool index or no-binding sentinel), compiles body with `helper_begin`, emits
POP_WITH. PUSH_WITH looks up the WITH extent at its PC and stores
`resume_pc = pop_pc + 1` on the node for use when exit swallows an exception.
The expression value is always `None`. Both sync and async context managers
are supported (VM auto-awaits enter/exit when they return awaitables). See
[interpreter/with-dyn-with.md](interpreter/with-dyn-with.md).

**Tests:** Opcode tests (`tests/lib/opcode/test_with.py`) and special-form
tests (`tests/compile/specials/test_with.py`) cover sync and async
enter/exit, normal completion, and exception propagation/swallow.

**Examples:**

```scheme
;; With binding
(with [f (open-file path)]
  (compile/call read-all f))

;; Without binding
(with [(make-lock)]
  (compile/call do-work))
```

## dyn-with

**Symbol:** `compile/dyn-with`

**Syntax:** `(dyn-with [name? expr] body ...)`

Same syntax as `with`; optional `name` binds the enter result.

**Use-case:** Multi-shot context manager that participates in continuation
transfer plans (shift/reset, undelimited call/cc). On non-local transfer,
enter/exit run as wind/unwind steps; re-entry creates a fresh extent
instance.

**Behaviour:** Same as `with` except emits PUSH_DYN_WITH and POP_DYN_WITH.
Expression value is always `None`. See
[interpreter/with-dyn-with.md](interpreter/with-dyn-with.md).

**Examples:**

```scheme
;; Capture continuation inside dyn-with; invoke outside runs exit then enter
(dyn-with [r (make-resource)]
  (compile/call k (compile/refq r)))
```

## compile/setq

**Symbol:** `compile/setq`

**Syntax:** `(compile/setq SYM EXPR)`

**Use-case:** Store the value of EXPR into the variable named SYM (must already
exist in scope or be requested from an enclosing scope). The form evaluates to
None.

**Behaviour:** Requests the variable name, compiles EXPR, emits VAR_STORE, then
pushes CONST None so the form has a value.

**Note:** The `set!` macro expands symbol assignment to this form; multi-value
assignment uses `compile/setq-values`. Augmented symbol assignment uses
`compile/inplace`. Item and attribute in-place updates use
`compile/inplace-item` and `compile/inplace-attr` (via the `compile/inplace`
macro extension). Plain item/attr/ref stores use the corresponding set specials.
See [MACROS.md](MACROS.md).

**Examples:**

```scheme
(compile/setq x 42)
(compile/setq counter (compile/call + counter 1))
```

## compile/setq-values

**Symbol:** `compile/setq-values`

**Syntax:** `(compile/setq-values PATTERN VALUE)`

PATTERN is either a symbol (single store) or a list pattern in the same shape as
`compile/for-each` unpack bindings (including `*:` rest segments).

**Use-case:** Store into existing variables by unpacking the value of VALUE
according to PATTERN. Evaluates VALUE once, then assigns components (or the
whole value for a symbol pattern).

**Behaviour:** Requests write access for every binder in the pattern, compiles
VALUE, then emits unpack store bytecode via the same helper as unpack-style
`using` bindings (`declare=False`). The form evaluates to None.

**Note:** The `set!` macro expands `(set! (values pattern ...) value)` to
`(compile/setq-values [pattern ...] value)`; see [MACROS.md](MACROS.md).

**Examples:**

```scheme
(compile/setq-values [a b] (compile/call pair 1 2))
(compile/setq-values [a *: b c] seq)

;; Via set! (see MACROS.md)
(set! (values x y) point)
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_setq_values`

**Tests:** `tests/compile/specials/test_var_specials.py`

## compile/define-values

**Symbol:** `compile/define-values`

**Syntax:** `(compile/define-values PATTERN VALUE)`

Same PATTERN shapes as `compile/setq-values`.

**Use-case:** Introduce new bindings and assign by unpacking the value of VALUE
according to PATTERN (parallel to `compile/define` for a single name).

**Behaviour:** Compiles VALUE, then emits unpack store bytecode with
`declare=True` for each binder (same helper as `compile/setq-values`, without
prior `request_for_write`). The form evaluates to None.

**Note:** The `let-values` macro sequences this form inside `let`; see
[MACROS.md](MACROS.md).

**Examples:**

```scheme
(compile/define-values [a b] (compile/call pair 1 2))
(compile/define-values [a *: b c] seq)

;; Via let-values (see MACROS.md)
(let-values [(x y) point] (use x y))
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_define_values`

**Tests:** `tests/compile/specials/test_var_specials.py`

## compile/inplace

**Symbol:** `compile/inplace`

**Syntax:** `(compile/inplace OP VAR VALUE)`

**Use-case:** In-place update of a **symbol** binding: compute `VAR OP VALUE` and
store the result back into VAR. Equivalent to Python augmented assignment on a
local/global name (`x += 1`, `x &= mask`, etc.). OP is any numeric or bitwise
operator; VAR must be a symbol.

For item or attribute places, use **`compile/inplace-item`** or
**`compile/inplace-attr`** (normally via the **`compile/inplace`** macro
extension — see [MACROS.md](MACROS.md)).

**Behaviour:** Compiles VALUE, emits the corresponding VAR_OBJECT_INPLACE_* opcode (arg =
name constant index), then CONST None. The compiler may lower VAR_OBJECT_INPLACE_* to
FAST_OBJECT_INPLACE_* when VAR has a fixed slot. VAR_OBJECT_INPLACE_* uses Python in-place
operators (`PyNumber_InPlaceAdd`, etc.).

**Supported operators:** `+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`, `<<`, `>>`,
`&`, `^`, `|` (and their `compile/` forms, e.g. `compile/+`, `compile/-`, etc.).

**Examples:**

```scheme
;; Via set! macro (see MACROS.md)
(set! x (+ x 1))    ; x += 1
(set! n (* n 2))    ; n *= 2
(set! bits (| bits mask))  ; bits |= mask

;; Direct use
(compile/inplace + counter 1)
(compile/inplace // total divisor)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace`

## compile/inplace-item

**Symbol:** `compile/inplace-item`

**Syntax:** `(compile/inplace-item OP OBJ KEY VALUE...)`

**Use-case:** In-place update of an item slot: load `OBJ[KEY]`, compute
`current OP VALUE`, store the result back at `KEY`. Equivalent to augmented
assignment on a subscript (`d[k] += 1`). Normally reached via
`(inplace OP (item OBJ KEY) VALUE)` (see [MACROS.md](MACROS.md)).

**Behaviour:** Compiles OBJ once. Loads the current item (CONST_ITEM when KEY is
constant, otherwise ITEM after KEY is evaluated). Uses **DUP** so OBJ (and KEY,
when not constant) is not re-evaluated for the store. Compiles the VALUEs,
applies the corresponding **OBJECT_INPLACE_*** opcode (the `PyNumber_InPlace*`
API, so mutable values are updated in place; multiple values (except for `**`, which takes exactly one) reduce
left-to-right through the single instruction), then stores via CONST_ITEM_STORE
or ITEM_STORE. Evaluates to None.

**Supported operators:** Same set as `compile/inplace`.

**Examples:**

```scheme
;; Via compile/inplace macro extension
(inplace + (item vec 0) 1)
(inplace - (item d "count") step)

;; Direct use
(compile/inplace-item + table key delta)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace_item`

**Tests:** `tests/compile/specials/test_inplace_specials.py`

## compile/inplace-attr

**Symbol:** `compile/inplace-attr`

**Syntax:** `(compile/inplace-attr OP OBJ ATTR VALUE...)`

**Use-case:** In-place update of an attribute: load `OBJ.ATTR`, compute
`current OP VALUE`, store the result back. Equivalent to augmented assignment on
an attribute (`obj.x += 1`). ATTR must be a symbol. Normally reached via
`(inplace OP (attr OBJ ATTR) VALUE)` (see [MACROS.md](MACROS.md)).

**Behaviour:** When OBJ is a symbol, loads via VAR_ATTR and stores via
VAR_ATTR_STORE (object from the namespace). Otherwise compiles OBJ once, **DUP**
1, loads via ATTR, compiles the VALUEs, applies **OBJECT_INPLACE_*** (the
`PyNumber_InPlace*` API, so mutable values are updated in place; multiple
values reduce left-to-right through the single instruction), stores via
ATTR_STORE. Evaluates to None.

**Supported operators:** Same set as `compile/inplace`.

**Examples:**

```scheme
;; Via compile/inplace macro extension
(inplace + (attr obj count) 1)

;; Direct use
(compile/inplace-attr + obj total amount)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace_attr`

**Tests:** `tests/compile/specials/test_inplace_specials.py`

## compile/refq

**Symbol:** `compile/refq`

**Syntax:** `(compile/refq SYM)`

**Use-case:** Yield a mutable reference (Ref) to the variable named SYM. The
reference can be passed and later deref'd or stored into.

**Behaviour:** Requests the variable name and emits VAR_REF. The value on the
stack is a Ref for that name.

**Examples:**

```scheme
(compile/refq my-var)
```

## compile/bindq

**Symbol:** `compile/bindq`

**Syntax:** `(compile/bindq SYM EXPR)`

**Use-case:** Introduce a new binding SYM in the current scope with the value of
EXPR. The form evaluates to None.

**Behaviour:** Declares the variable name, compiles EXPR, emits VAR_BIND, then
pushes CONST None.

**Examples:**

```scheme
(compile/bindq local 10)
```

## compile/define

**Symbol:** `compile/define`

**Syntax:** The name and optional return type can be written in either of two
ways:

**Inline type** (preferred; same `::` / `type:` placement as `compile/lambda`):

- Declaration only: `(compile/define NAME :: TYPE)` or `(compile/define NAME type: TYPE)`
- Declare and store: `(compile/define NAME :: TYPE EXPR)` (or `type: TYPE`)
- Function shorthand:
`(compile/define NAME :: TYPE FORMALS BODY ...)` (or `type: TYPE`)

**DECL** as the first argument (single binding-shaped form, no value):

- Declaration only: `(compile/define DECL)`
- Declare and store: `(compile/define DECL EXPR)`
- Function shorthand: `(compile/define DECL FORMALS BODY ...)`

**DECL** may be:

- A **symbol** (e.g. `x`), or
- A **proper list** introducing a name with optional **type** attributes only:
e.g. `[x]`, `[x :: int]`, `[x type: int]`, or `(TYPE as: NAME)` for the
type-as-name spelling. **`value:`** and two-element **`(SYM EXPR)`** defaults
are rejected (declarations do not carry a value).

**Use-case:** Variable and function definition for module-level loading.
Declaration-only leaves the ref in scope but empty (reading before assignment
raises). Declare-and-store evaluates EXPR and stores the result in the slot
(equivalent to compile/bindq). Function shorthand lowers to
`compile/function`: if a return type is present (inline or in `DECL`), the
expansion is `(compile/function NAME :: TYPE FORMALS BODY ...)` (using the `::`
keyword); if not, `(compile/function NAME FORMALS BODY ...)`.

For declaration-only or value binding with a type (e.g.
`(compile/define [x :: int])` or `(compile/define x :: int 99)`), the type is
accepted by the parser but is not stored on the variable binding in the current
compiler (parse-only / metadata for tooling), except that function shorthand
passes the type through to `compile/function` as the return annotation.

Recursive calls in function shorthand (e.g. `fact` below) use the **module/local
binding** after `VAR_STORE`, not an inner closure self-ref for the name.

**Behaviour:** With **DECL**, parses the first argument with `parse_decl`. With
**inline type**, the matcher binds `NAME`, `::` or `type:`, and `TYPE` (see
[MATCHER.md](MATCHER.md) — **DECL** composite). Declares
the introduced name. For declaration-only: emits CONST None. For
declare-and-store: compiles EXPR, emits `VAR_STORE` to store the value, then
CONST None. For function shorthand: builds the `compile/function` form as above and
compiles it, then emits `VAR_STORE`.

**Examples:**

```scheme
;; Declaration only; assign later with compile/setq
(compile/define x)
(compile/setq x 42)

;; Typed declaration (parse-only on the binding; assign later as usual)
(compile/define x :: int)
(compile/define [x :: int])

;; Declare and store
(compile/define y 99)

;; Function shorthand
(compile/define double [x] (* x 2))
(compile/define f :: int [x] 5)
(compile/define [f :: int] [x] 5)
(compile/define fact [n]
  (compile/cond [(== n 0) 1]
                [else: (* n (fact (- n 1)))]))
```

**Tests:** `tests/compile/specials/test_define.py`

## compile/define-global

**Symbol:** `compile/define-global`

**Syntax:** Same arities and spellings as `compile/define`: **inline type**
(`NAME :: TYPE` / `NAME type: TYPE`) or **DECL** as the first argument:

- `(compile/define-global NAME :: TYPE)` … (and `type:`), or `(compile/define-global DECL)`
- `(compile/define-global NAME :: TYPE EXPR)` or `(compile/define-global DECL EXPR)`
- `(compile/define-global NAME :: TYPE FORMALS BODY ...)` or
`(compile/define-global DECL FORMALS BODY ...)`

See **compile/define** for the meaning of **DECL** and function-shorthand
lowering to `compile/function` (including `NAME :: TYPE` when a return type is
present). Same inner form as `compile/define`; additionally calls
`request_global(name)` and stores via `VAR_STORE` into the module binding.

**Use-case:** Explicit module binding for read and write. When using strict
resolution (KoskariPragma), writes to undeclared names raise at compile time;
`compile/define-global` opts a name into the module scope so that setq/delq
target the module binding rather than requiring a local declaration.
Mirrors `compile/define` structure but pulls the ref from the module instead
of the local or closure chain.

**Behaviour:** Calls `request_global(name)` to record an explicit need from the
module. For declaration-only: emits CONST None. For declare-and-store or
function shorthand: compiles the value expression and emits VAR_STORE. Conflicts
with local declaration; raises if the name is already in `declares` (e.g. from
`compile/define`).

**Examples:**

```scheme
;; Explicit global; setq writes to module
(compile/define-global x 1)
(compile/setq x 42)

;; Shared counter across nested closures
((lambda ()
   (compile/define-global counter 0)
   (define inc (lambda []
     (compile/setq counter (compile/call + counter 1))
     counter))
   (compile/call inc)))
```

**Tests:** `tests/compile/specials/test_resolution_model.py`

**Implementation:** `koskari/compile/specials/variables.special_define_global`

## compile/define-syntax

**Not a compiler special.** User-facing **`(define-syntax NAME TRANSFORMER)`** is
expanded by the **`compile/define-syntax`** **macro** in
`koskari/bootstrap/_define_syntax.ki` (there is no registered `@special` for
`compile/define-syntax`). The macro rewrites to **`(compile/define NAME …)`** with a
transformer whose head is **`syntax-rules`**, **`macro-rules`**, **`syntax-match`**,
**`macro-match`**, **`special-rules`**, or **`special-match`**. Those specs are
lowered by **`compile/syntax-rules`**, **`compile/macro-rules`**,
**`compile/syntax-match`**, **`compile/macro-match`**, **`compile/special-rules`**,
and **`compile/special-match`** (below).

**Typical surface form:** **`(define-syntax NAME (syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...))`**
— the macro expands that to **`compile/define`** plus a **`syntax-rules`** spec
with **`NAME`** repeated as the first argument. Block syntax via
`layout/define-syntax` and `layout/syntax-rules` produces the same shape; see
[LAYOUTS.md](LAYOUTS.md).

**Implementation:** `koskari/bootstrap/_define_syntax.ki` (macro definition).

**See also:** [MACROS.md](MACROS.md) — **compile/define-syntax (macro)**;
[MATCHER.md](MATCHER.md) — matcher syntax, composites, and hygiene tables;
[language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md)
(**RULE** vs **MATCH** pattern languages; unified matcher IR, transformer
pipeline, and post-expand hygiene/unbox for `define-syntax`, `macro-rules`, and
specials).


## compile/syntax-rules

**Symbol:** `compile/syntax-rules`

**Syntax:** `(compile/syntax-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`syntax-rules`**-style transformer spec to a
**`build-syntax-rules`** call form (for recompilation). The built transformer is
the **value** of that expression; bind it with **`(compile/define NAME …)`**.

**Behaviour:** Parses literals and implicit-pattern rules, builds a
`SyntaxRulesTransformer`, emits **`(build-syntax-rules NAME TAGGED)`**.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_syntax_rules`

**Tests:** `tests/compile/specials/test_define_syntax.py`

**See also:** [MATCHER.md](MATCHER.md) — **`compile/syntax-rules`** hygiene
patterns (`DEFINE`, `BIND` for literals, `REST` for rules).


## compile/macro-rules

**Symbol:** `compile/macro-rules`

**Syntax:** `(compile/macro-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`macro-rules`**-style transformer spec to a
**`build-macro-rules`** call form. Bind the result with **`compile/define`**.

**Behaviour:** Parses literals and implicit-pattern macro rules, emits
**`build-macro-rules`** with serialized pattern matchers and per-rule lambdas.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_macro_rules`

**Tests:** `tests/compile/specials/test_define_syntax.py`


## compile/syntax-match

**Symbol:** `compile/syntax-match`

**Syntax:** `(compile/syntax-match NAME (PATTERN TEMPLATE) ...)`

**Use-case:** **MATCH**-dialect pattern rules (same emission path as
**`compile/syntax-rules`**: **`build-syntax-rules`**).

**Behaviour:** Each rule row is **`(PATTERN TEMPLATE)`** where **`PATTERN`** is
lowered by **`parse_explicit_rule_pattern`** in `koskari/syntax.py` (explicit
matcher declarations such as **`(VALUE x)`**, **`(REPEAT … elem: …)`**, keyword
options on registered matchers, etc.). See
**[language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md)**
and **[MATCHER.md](MATCHER.md)** (`parse_explicit_rule_pattern`, surface heads).

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_syntax_match`

**Tests:** `tests/compile/specials/test_define_syntax.py`


## compile/macro-match

**Symbol:** `compile/macro-match`

**Syntax:** `(compile/macro-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect procedural rules (same emission shape as
**`compile/macro-rules`**: **`build-macro-rules`**).

**Behaviour:** Each rule row is **`(PATTERN BODY …)`**; **`PATTERN`** is passed to
**`parse_explicit_rule_pattern`**. Per-rule **`compile/lambda`** formals are
**`matcher_slot_names(pattern_matcher)`** in consume order (see
`koskari/matcher.py` and [MATCHER.md](MATCHER.md)). Macro procedures are invoked
with **keyword** arguments from the match result (`koskari/syntax.py`:
**`MacroRule.expand`**).

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_macro_match`

**Tests:** `tests/compile/specials/test_define_syntax.py`


## compile/special-rules

**Symbol:** `compile/special-rules`

**Syntax:** `(compile/special-rules NAME LITERALS RULE ...)`

**Use-case:** Lower a **`special-rules`**-style transformer spec to a
**`build-special-rules`** call form. Rule procedures are **compiler-aware**:
each rule’s lambda is built with formals **`(COMPILER TC pattern-vars …)`** —
the **`COMPILER`** and **`tc`** slots correspond to the pattern column breakout
(see behaviour). Bind the result with **`compile/define`**.

**Behaviour:** Like **`compile/macro-rules`**, but each rule’s pattern column must
be **`(compiler-formal pattern [tc-formal])`**: the first subform names the
compiler parameter, the second is a nested **RULE** pattern (lowered with
**`parse_rule_pattern`**), and the optional third names the tail-call formal
(defaults to a fresh **`gensym`**). Per-rule **`compile/lambda`** formals are
**`compiler-formal`**, **`tc-formal`**, then pattern variables from the inner
RULE pattern (same ordering as **`compile/macro-rules`** for implicit patterns).
The literal set includes the macro **`NAME`** automatically (as for other
rule-based transformers). The built transformer’s **`compile_impl`** passes
**`compiler`** and **`tc`** into the winning rule and returns **`unbox_macro`**
of the result (see **`SpecialRulesTransformer`** in **`koskari/syntax.py`**).

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_special_rules`

**Tests:** `tests/compile/specials/test_define_syntax.py`


## compile/special-match

**Symbol:** `compile/special-match`

**Syntax:** `(compile/special-match NAME (PATTERN BODY ...) ...)`

**Use-case:** **MATCH**-dialect special rules (same **`build-special-rules`**
lowering as **`compile/special-rules`**, with an empty literals tuple — same
overall shape as **`compile/macro-match`**). Rule bodies receive **`compiler`**,
**`tc`**, and matcher slots, as for **`compile/special-rules`**.

**Behaviour:** Each rule’s pattern column must be **`(compiler-formal pattern [tc-formal])`**;
the inner **`pattern`** is lowered by **`parse_explicit_rule_pattern`** (explicit
MATCH syntax). If that inner pattern is a bare symbol, it is wrapped as
**`(REST sym)`** before parsing (same convenience as **`compile/macro-match`**).
Emits **`build-special-rules`** with **`()`** for the serialized literals tuple.

**Implementation:** `koskari/compile/specials/syntax.py` —
`special_compile_special_match`

**Tests:** `tests/compile/specials/test_define_syntax.py`


## compile/class

**Symbol:** `compile/class`

**Syntax:** `(compile/class NAME (SUPERS) BODY ...)`

- **NAME**: symbol — used for the Python class `__name__` (and the BUILD_CLASS
  position tuple); **not** a compile-time binding target for this special
- **SUPERS**: list of superclass expressions and optional `meta: Metaclass` keyword
- **BODY**: forms compiled in sequence inside the CollectorNamespace (e.g.
  `compile/define` for class attributes and methods)

**Use-case:** Create a Koskari class via PUSH_COLLECT/POP_COLLECT/BUILD_CLASS and
run the body in a CollectorNamespace. The **value of the form** is the new class
object (default metaclass is KosType; use `meta: MyMeta` in SUPERS for a custom
metaclass). To **bind** the class to a name in the surrounding scope, wrap with
`compile/define` (or use the **`define-class`** macro — see
[MACROS.md](MACROS.md) § compile/define-class).

**Behaviour:** Builds a position tuple `(name, bases)` or
`(name, bases, metaclass)` with BUILD_ARGS, opens a collector namespace
(PUSH_COLLECT / POP_COLLECT around the body), compiles **BODY** with
`helper_begin`, then emits **BUILD_CLASS**. Unless the body already declares
them, the compiler stores **`__qualname__`** as **NAME** (CPython-style,
scope-only; nested `Outer.Inner` would need a future nesting stack) and
**`__module__`** from the compiling **`Module.__name__`**, falling back to
**`compiler.name`**, and skips **`__module__`** injection when no non-empty
string is found. A
leading string in **BODY** is stripped as the class
`__doc__` when `__doc__` is not already declared (same pattern as functions).
The collect scope uses a stack block so body expression results (e.g. `None`
from `compile/define`) do not reach **BUILD_CLASS**. The class object is left on
the stack as the expression result. This special does **not** emit `declare_var`
or `VAR_STORE` for **NAME** itself (only for injected `__doc__` /
`__qualname__` / `__module__` when applicable).

**Examples:**

```scheme
;; Expression value is the class (e.g. bind with compile/define)
(compile/define C (compile/class C ()))

;; Class with attribute
(compile/define C
  (compile/class C ()
    (compile/define x 42)))

;; Class with superclass (object is available from bootstrap)
(compile/define C
  (compile/class C (object)
    (compile/define x 1)))

;; Custom metaclass
(compile/define C
  (compile/class C (Base meta: MyMeta)
    (compile/define x 1)))
```

**Tests:** `tests/compile/specials/test_class.py`

## compile/delq

**Symbol:** `compile/delq`

**Syntax:** `(compile/delq SYM)`

**Use-case:** Delete the binding named SYM from the current scope. The form
evaluates to None.

**Behaviour:** Requests the variable name, emits VAR_CLEAR, then pushes CONST
None.

**Examples:**

```scheme
(compile/delq temp)
```

## compile/takeq

**Symbol:** `compile/takeq`

**Syntax:** `(compile/takeq SYM)`

**Use-case:** Return the value of binding SYM and clear the slot. Unlike
`compile/delq`, errors when the binding is empty. Used to return an accumulator
and drop a gensym reference (e.g. `list-each` ends with `(takeq col)`).

**Behaviour:** `request_var`, emit VAR_TAKE (may lower to FAST_TAKE). No CONST
None tail; the form value is the taken value.

**Examples:**

```scheme
(compile/takeq col)
```

**Tests:** `tests/compile/specials/test_take_specials.py`

## compile/attr

**Symbol:** `compile/attr`

**Syntax:** `(compile/attr OBJ SYM)`

**Use-case:** Load the attribute named SYM from OBJ (getattr). The value is
pushed on the stack.

**Behaviour:** Compiles OBJ, then emits ATTR with the attribute name (string).

**Examples:**

```scheme
(compile/attr obj "value")
(compile/attr thing count)
```

## compile/del-attr

**Symbol:** `compile/del-attr`

**Syntax:** `(compile/del-attr OBJ SYM)`

**Use-case:** Delete the attribute named SYM on OBJ. The form evaluates to None.

**Behaviour:** Compiles OBJ, emits ATTR_CLEAR, then pushes CONST None.

**Examples:**

```scheme
(compile/del-attr obj x)
```

## compile/take-attr

**Symbol:** `compile/take-attr`

**Syntax:** `(compile/take-attr OBJ SYM)`

**Use-case:** Get the attribute named SYM on OBJ, delete it, and return the
value.

**Behaviour:** Compiles OBJ, emits ATTR_TAKE or VAR_ATTR_TAKE when OBJ is a
symbol. Pushes the removed value (no CONST None).

**Examples:**

```scheme
(compile/take-attr obj x)
```

## compile/set-attr

**Symbol:** `compile/set-attr`

**Syntax:** `(compile/set-attr OBJ SYM VAL)`

**Use-case:** Set the attribute named SYM on OBJ to the value of VAL. The form
evaluates to None.

**Behaviour:** Compiles OBJ, compiles VAL, emits ATTR_STORE with the attribute
name, then pushes CONST None.

**Examples:**

```scheme
(compile/set-attr obj "value" 42)
```

## compile/ref-attr

**Symbol:** `compile/ref-attr`

**Syntax:** `(compile/ref-attr OBJ SYM)`

**Use-case:** Yield a mutable reference (AttributeRef) to the attribute named
SYM on OBJ. The reference can be deref'd or stored into.

**Behaviour:** Compiles OBJ, then emits ATTR_REF with the attribute name.

**Examples:**

```scheme
(compile/ref-attr obj count)
```

## compile/item

**Symbol:** `compile/item`

**Syntax:** `(compile/item OBJ KEY)`

**Use-case:** Load the item at key KEY from OBJ (subscript/getitem). If KEY
compiles to a single constant, uses CONST_ITEM; otherwise compiles KEY and
emits ITEM.

**Behaviour:** Compiles OBJ. If the key expression is constant-only (per
`const_only_expr`), emits CONST_ITEM with that constant; otherwise merges the
key fragment and emits ITEM 0.

**Examples:**

```scheme
(compile/item vec 0)
(compile/item tbl (compile/call compute-key))
```

## compile/ref-item

**Symbol:** `compile/ref-item`

**Syntax:** `(compile/ref-item OBJ KEY)`

**Use-case:** Yield a mutable reference (ItemRef) to the item at KEY in OBJ. Same
constant-key optimization as compile/item.

**Behaviour:** Like compile/item but emits CONST_ITEM_REF or ITEM_REF.

**Examples:**

```scheme
(compile/ref-item lst i)
```

## compile/deref

**Symbol:** `compile/deref`

**Syntax:** `(compile/deref EXPR)`

**Use-case:** Dereference a mutable reference. Compiles EXPR (which must produce
a Ref on the stack), then emits the DEREF opcode to call `ref.deref()` and push
the result. The `*identifier` reader atom expands to `(compile/deref identifier)`.
The runtime `deref` builtin remains available for higher-order use (e.g.
`(map deref refs)`).

**Behaviour:** Compiles EXPR, emits KOS_OP_DEREF. Fast path; avoids a full call.

**Examples:**

```scheme
(compile/deref r)   ; compiles expr for r, emits DEREF
*r                  ; reader: (compile/deref r)
(deref r)           ; runtime call; use when passing deref as first-class fn
```

## compile/del-item

**Symbol:** `compile/del-item`

**Syntax:** `(compile/del-item OBJ KEY)`

**Use-case:** Delete the item at KEY in OBJ. The form evaluates to None. Same
constant-key optimization as compile/item.

**Behaviour:** Compiles OBJ, then either CONST_ITEM_CLEAR or key fragment +
ITEM_CLEAR, then CONST None.

**Examples:**

```scheme
(compile/del-item d "key")
```

## compile/take-item

**Symbol:** `compile/take-item`

**Syntax:** `(compile/take-item OBJ KEY)`

**Use-case:** Get the item at KEY in OBJ, delete it, and return the value.
Same constant-key optimization as compile/item.

**Behaviour:** Compiles OBJ; if key is constant-only, emits CONST_ITEM_TAKE;
otherwise merges key fragment and emits ITEM_TAKE.

**Examples:**

```scheme
(compile/take-item d "key")
```

## compile/set-item

**Symbol:** `compile/set-item`

**Syntax:** `(compile/set-item OBJ KEY VAL)`

**Use-case:** Set the item at KEY in OBJ to VAL. The form evaluates to None.
Same constant-key optimization as compile/item.

**Behaviour:** Compiles OBJ; if key is constant, compiles VAL and emits
CONST_ITEM_STORE; otherwise merges key fragment, compiles VAL, emits
ITEM_STORE 0. Then CONST None.

**Examples:**

```scheme
(compile/set-item vec 0 99)
```

## compile/quote

**Symbol:** `compile/quote`

**Syntax:** `(compile/quote FORM)`

**Use-case:** Prevent evaluation; FORM is produced as a literal value. For
atoms, a single CONST is emitted. For non-nil lists, the form is rewritten to
(compile/cons (compile/quote e1) (compile/quote e2) ...) and that form is
compiled so each element is quoted and combined via the CONS opcode.

**Behaviour:** One argument required. If FORM is a non-nil pair, rewrite with
`requote` to a list of quoted elements (using compile/cons and compile/quote), set
position on the result, and return that pair so the compiler re-dispatches and
compiles it. Otherwise emit CONST FORM.

**Examples:**

```scheme
(compile/quote 42)
(compile/quote x)
(compile/quote (a b c))
```

## compile/quasiquote

**Symbol:** `compile/quasiquote`

**Syntax:** ``EXPR` (reader shorthand) or `(compile/quasiquote EXPR)`

**Use-case:** Quasi-quotation: construct a pair structure at compile time with
selected sub-expressions evaluated at runtime. Inside the body, `,EXPR`
(unquote) splices a single evaluated value, and `,@EXPR` (unquote-splicing)
flattens a list into the surrounding list.

**Reader shorthands:**

- ``EXPR` → `(compile/quasiquote EXPR)`
- `,EXPR` → `(compile/unquote EXPR)` (only valid inside a quasiquote body)
- `,@EXPR` → `(compile/unquote-splicing EXPR)` (only valid inside a quasiquote body)
- ``HEAD: BODY` → `(compile/quasiquote-block (:block: ...))` (block quasiquote; see below)

**Behaviour:** One argument required. The compiler walks the expression
recursively at a nesting *level* (starting at 0):

- **Atoms and constants** (symbols, keywords, numbers, nil): emitted as `CONST`.
- `**(compile/unquote INNER)`** at level 0: `INNER` is compiled normally
(evaluated at runtime); its result becomes that position's value.
- `**(compile/unquote INNER)**` at level > 0: the unquote form is reconstructed
literally as a pair — `CONST compile/unquote`, recurse on INNER at level − 1,
`PROPER 2`.
- `**(compile/quasiquote INNER)**` at any level: reconstructed literally at
level + 1.
- **Proper lists**: walked element by element. Consecutive non-splice elements
are accumulated into *segments*:
  - If no `unquote-splicing` site is present anywhere in the list, each segment
  is emitted individually with `PROPER n`.
  - If any splice is present, constant-only segments are folded into a single
  `CONST (e0, e1, ...)` tuple (which `PAIR_SPLICE` accepts natively); mixed
  segments use `PROPER n`. Each splice site compiles its inner expression
  directly (must produce an iterable pair or nil). All segments and splices are
  gathered then combined with `PAIR_SPLICE count`.
- **Dotted (improper) pairs**: like a proper list but the non-nil tail is emitted
after all elements and the whole thing is closed with `CONS count+1` (the tail
is explicit). Splice in dotted-tail position raises `SpecialError`.

**Nesting:** Each additional ``` increments the level; each `,` decrements
it. At level > 0, unquote forms are data, not escapes.

**Note on dotted tails with unquote:** `(a . ,x)` is parsed by the reader as the
three-element proper list `(a compile/unquote x)` (because `(a . (unquote x))`
collapses to a proper list). Only atomic tails produce genuine improper pairs
(e.g. ``(a . 42)` or ``(a . b)`).

**Opcodes emitted:** `CONST`, `PROPER`, `CONS`, `PAIR_SPLICE` (see OPCODES.md §5.1).

**Examples:**

```scheme
;; Atom: just a constant
`42           ; → 42
`foo          ; → symbol foo

;; Proper list — compiles to PROPER 3
`(a b c)      ; → (a b c)

;; Unquote — x is evaluated at runtime
`(a ,x b)     ; → (a <value of x> b)

;; Unquote-splicing — items is spliced in
`(a ,@items b) ; → (a item0 item1 ... b)

;; Dotted pair with atom tail
`(a . 42)     ; → (a . 42)

;; Nested quasiquote — inner unquote is NOT evaluated at outer level
``(a ,(+ 1 2)) ; → (compile/quasiquote (a (compile/unquote (+ 1 2))))
```

**Implementation:** `koskari/compile/specials/quasiquote.py`

**Tests:** `tests/compile/specials/test_quasiquote.py`

## compile/quote-block

**Symbol:** `compile/quote-block`

**Syntax:** `'HEAD: BODY` (reader shorthand) or `(compile/quote-block BLOCK-REPR)`

**Use-case:** Produce a `:block:` cons as literal data — the block-syntax
analogue of `compile/quote`. The entire indented block (including any nested
sub-blocks) is captured as a `:block:` cons without invoking any layout
handler. Useful for passing block structure as data to macros or other
compile-time code.

**Reader shorthand:** A `'` immediately preceding a block head (e.g. `'begin: ...`) causes the reader to read the entire block as a raw `:block:` cons and
wrap it in `(compile/quote-block ...)`. Sub-blocks in the body are recursively
read as `:block:` nodes without layout expansion.

**Behaviour:** Purely transformative. Rewrites to `(compile/quote BLOCK-REPR)`
and returns that cons for normal compiler dispatch. The `:block:` cons is
treated as opaque data — no layout expansion takes place.

**Examples:**

```scheme
;; Reader shorthand: produces (:block: (begin) (foo bar)) as data
'begin:
  foo
  bar

;; Explicit form
(compile/quote-block (:block: (begin) (foo bar)))
```

**Implementation:** `koskari/compile/specials/quasiquote.py`

**Tests:** `tests/compile/specials/test_quasiquote.py`, `tests/reader/layouts/test_quasiquote.py`

## compile/quasiquote-block

**Symbol:** `compile/quasiquote-block`

**Syntax:** ``HEAD: BODY` (reader shorthand) or `(compile/quasiquote-block BLOCK-REPR)`

**Use-case:** The block-syntax analogue of `compile/quasiquote`. Captures an
entire indented block (including nested peer blocks such as `if`/`else`) as a
`:block:` cons with `,EXPR` and `,@EXPR` unquote sites active throughout the
header and body. The primary tool for writing macros in block syntax.

**Reader shorthand:** A ``` immediately preceding a block head causes the
reader to read the entire block as a raw `:block:` cons with `,`/`,@` unquote
reader macros active for all header arguments, body forms, and nested
sub-block headers and bodies.

```scheme
`begin:
  ,@forms
```

produces:

```
(compile/quasiquote-block (:block: (begin) ((compile/unquote-splicing forms))))
```

Peers are represented as sibling `:block:` nodes inside the enclosing body:

```scheme
`begin:
  if ,cond:
    ,then
  else:
    ,else
```

produces:

```
(compile/quasiquote-block
  (:block: (begin)
   ((:block: (if (compile/unquote cond)) ((compile/unquote then)))
    (:block: (else) ((compile/unquote else))))))
```

**Behaviour:** Purely transformative. Rewrites to `(compile/quasiquote
BLOCK-REPR)` for normal compiler dispatch. `compile/quasiquote` walks the
`:block:` structure and fills unquote / unquote-splicing sites; it does **not**
invoke layout handlers, so the filled structure remains `:block:` reader form
where the quasi-quote produced blocks.

For **`define-syntax`** expansions, **`koskari.syntax._finalize_macro_output`**
(applied after **`MacroTransform`** handlers and **`SyntaxTransform`** template
substitution) calls **`koskari.reader.layouts.apply_layout`** on the expanded
form using **`vars(compiler.module)`** **before** binding-symbol discovery and
the shared hygiene / **`Datum`** finalize pass. That performs depth-first
`:block:` expansion through **`layout/<head>`** handlers with peer consumption,
matching reader layout semantics. Custom expansion pipelines that bypass that
finalize step must call **`apply_layout`** (or **`apply_layouts`**) themselves
when layout-expanded source is required.

**Nesting:** `_qq_expr` recognises `compile/quasiquote-block` at depth > 0
and reconstructs it as literal data, exactly like nested `compile/quasiquote`.

**Implementation:** `koskari/compile/specials/quasiquote.py`

**Tests:** `tests/compile/specials/test_quasiquote.py` (`TestQuasiquoteBlockShape`,
`TestQuasiquoteBlockTwoPhase`, `TestQuasiquoteBlockFullStack`),
`tests/reader/layouts/test_quasiquote.py`; runtime layout expansion behaviour:
`tests/reader/layouts/test_apply_layout_runtime.py`

## compile/unquote

**Symbol:** `compile/unquote`

**Syntax:** `,EXPR` (reader shorthand) or `(compile/unquote EXPR)`

**Use-case:** Escape from quasi-quotation. Only valid inside a
`compile/quasiquote` body; at the active nesting level (level 0) the argument
is compiled and evaluated normally, and its result is inserted at that position.

**Behaviour:** When called as a top-level special form (outside quasiquote),
raises `SpecialError`. Inside quasiquote, this form is handled directly by the
`compile/quasiquote` compiler and never reaches the `compile/unquote` handler.

**Examples:**

```scheme
`(a ,x b)    ; x is evaluated
`(a ,(+ 1 2) b)  ; (+ 1 2) is evaluated, giving 3
```

## compile/unquote-splicing

**Symbol:** `compile/unquote-splicing`

**Syntax:** `,@EXPR` (reader shorthand) or `(compile/unquote-splicing EXPR)`

**Use-case:** Splice a list into a quasi-quoted list. Only valid in list
(element) position inside a `compile/quasiquote` body; the argument must
evaluate to a pair list or nil at runtime, and its elements are inserted
into the surrounding list.

**Behaviour:** When called as a top-level special form (outside quasiquote),
raises `SpecialError`. When the enclosing list is being compiled, splice sites
are detected by the quasiquote compiler and compiled directly; the spliced
value is pushed onto the stack as a `PAIR_SPLICE` argument. Not valid in the
cdr (dotted tail) position of an improper list; the quasiquote compiler raises
`SpecialError` if a splice form appears there.

**Examples:**

```scheme
`(a ,@items b)   ; items (a list) is spliced in between a and b
`(,@xs ,@ys)     ; two lists concatenated
```

## compile/car

**Symbol:** `compile/car`

**Syntax:** `(compile/car EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the car of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CAR 1**. At runtime, CAR applies car
once per unit of arg (see [OPCODES §5.1](OPCODES.md#kos_op_car)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CAR**
instructions may be merged by the compiler **compress** pass into one **CAR**
with the sum of their counts (same for **CDR**).

**Example:**

```scheme
(compile/car (compile/cons 1 nil))   ; → 1
```

## compile/cdr

**Symbol:** `compile/cdr`

**Syntax:** `(compile/cdr EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the cdr of that pair. One argument.

**Behaviour:** Compile EXPR, then emit **CDR 1**. At runtime, CDR applies cdr
once per unit of arg (see [OPCODES §5.1](OPCODES.md#kos_op_cdr)). If a popped
value is not a pair or is nil, TypeError is raised. Consecutive **CDR**
instructions may be merged by the compiler **compress** pass into one **CDR**
with the sum of their counts (same for **CAR**).

**Example:**

```scheme
(compile/cdr (compile/cons 1 (compile/cons 2 nil)))   ; → (2 . nil)
```

## compile/#list

**Symbol:** `compile/#list`

**Syntax:** `(compile/#list EXPR ...)`

**Surface syntax:** `(#list EXPR ...)` and reader `#[ ... ]` (see
[SYNTAX.md](SYNTAX.md)). The compiler resolves `#list` to `compile/#list` via
`find_compiled`.

**Use-case:** Build a Python `list` from evaluated elements. Replaces the
bootstrap `build_list` fastcall on the compile path for `#list` forms.

**Behaviour:** Read the body with `gather_parameters`. A keyword / `**:`
argument defers to the runtime builtin via `(compile/call #list . BODY)`.
Otherwise the positional / `*:` elements are grouped by
`partition_collection_runs` (classifying constants via `const_only_expr`) into
constant runs, dynamic-expression runs, and `*:` splats, and the list is seeded
fresh then accumulated in place:

- **All-constant body** → single **BUILD_LIST_CONST** (pooled tuple → fresh
  list).
- **Seed** (first chunk): const run → **BUILD_LIST_CONST**; dynamic run of k →
  **BUILD_LIST k**; `*:` splat → **BUILD_LIST 0** + iterable +
  **BUILD_LIST_EXTEND**.
- **Tail** chunks: const run length ≥ 2 → **BUILD_LIST_EXTEND_CONST**; const
  run length 1 → **BUILD_LIST_APPEND_CONST**; dynamic run of k →
  **BUILD_LIST_APPEND k**; `*:` splat → iterable + **BUILD_LIST_EXTEND**.

Empty body emits **BUILD_LIST 0**. Each evaluation produces a new list; a list
is never placed in the constant pool. See
[archive/const-collection-squashing.md](archive/const-collection-squashing.md).

**Examples:**

```scheme
(#list)              ; → []
(#list 1 2 3)        ; → [1, 2, 3]  (single BUILD_LIST_CONST)
(#list x y)          ; → [x, y] with dynamic elements
(#list 1 x 2)        ; → [1, x, 2] mixed const/dynamic runs
(#list 1 2 *: xs)    ; → [1, 2, *xs] splat via BUILD_LIST_EXTEND
```

**Implementation:** `koskari/compile/specials/collections.py`

**Tests:** `tests/compile/specials/test_collections.py`

## compile/#list-append

**Symbol:** `compile/#list-append`

**Syntax:**

- `(compile/#list-append SYM EXPR)`
- `(compile/#list-append LIST_EXPR EXPR)`

**Surface syntax:** `(#list-append SYM EXPR)` or `(#list-append LIST EXPR)`.

**Use-case:** Append the value of EXPR to a list binding or to a list value on
the stack. Used by macros such as `list-each` (`(#list-append col item)`).

**Behaviour:**

- **Symbol binding:** `request_for_write` on SYM; compile EXPR; emit
  **VAR_LIST_APPEND** (may lower to **FAST_LIST_APPEND**); push **CONST None**
  (form value is `None`).
- **List expression:** compile LIST_EXPR, then EXPR; emit **BUILD_LIST_APPEND 1**
  (mutates the list in place; leaves the list on the stack). `BUILD_LIST_APPEND`
  takes a COUNT arg — the number of stack values to append — and this path
  appends the single EXPR value.

**Examples:**

```scheme
(begin
  (setq col (#list))
  (#list-append col 9)
  col)                 ; → [9]

(#list-append (#list 1) 2)   ; → [1, 2]
```

**Implementation:** `koskari/compile/specials/collections.py`

**Tests:** `tests/compile/specials/test_collections.py`

## compile/#tuple

**Symbol:** `compile/#tuple`

**Syntax:** `(compile/#tuple EXPR ...)`

**Surface syntax:** `(#tuple EXPR ...)` and reader `#( ... )` (see
[SYNTAX.md](SYNTAX.md)).

**Use-case:** Build a Python `tuple` from evaluated elements on the compile
path. Replaces the bootstrap `build_tuple` fastcall for `#tuple` forms.

**Behaviour:** Read the body with `gather_parameters`. A keyword / `**:`
argument defers to the runtime builtin via `(compile/call #tuple . BODY)`.
Otherwise the positional / `*:` elements are grouped by
`partition_collection_runs` into constant runs, dynamic-expression runs, and
`*:` splats, and each chunk emits one stack sequence: const run → **CONST**
(pooled tuple); dynamic run of k → **BUILD_ARGS k**; `*:` splat → the iterable
itself. A single const or dynamic chunk stands alone; otherwise (multiple
chunks, or a lone splat) **COMBINE_ARGS** over the chunk count concatenates them
into one fresh tuple. So an all-constant body is a single **CONST**, and a lone
splat still combines (count 1) to copy the iterable into a new tuple rather than
alias it. Empty body emits **BUILD_ARGS 0**. See
[archive/const-collection-squashing.md](archive/const-collection-squashing.md).

**Examples:**

```scheme
(#tuple)             ; → ()
(#tuple 1 2 3)       ; → (1, 2, 3)  (single CONST)
(#tuple x y)         ; → (x, y) with dynamic elements
(#tuple 1 x 2)       ; → (1, x, 2) mixed runs via COMBINE_ARGS
(#tuple 1 2 *: xs)   ; → (1, 2, *xs) splat via COMBINE_ARGS
```

**Implementation:** `koskari/compile/specials/collections.py`

**Tests:** reader `#(` lowering in `tests/reader/test_parse.py`; compile special
coverage in `tests/compile/specials/test_collections.py`.

## compile/cons

**Symbol:** `compile/cons`

**Syntax:** `(compile/cons EXPR1 EXPR2 ... EXPR_TAIL)`

**Use-case:** Variadic pair construction. Equivalent to (cons EXPR1 (cons EXPR2
(... (cons EXPR_TAIL) ...))). At least one argument (the tail).

**Behaviour:** Compile each expression in order (stack bottom = first element,
top = tail). Emit CONS with arg = number of expressions. VM builds the chain
via KosPair_ConsFromPtr(TailPtr(block, count), count), then Drop(count) and
push result. count 0 pushes nil.

**Example:**

```scheme
(compile/cons 1 2 nil)                    ; → (1 . (2 . nil))
(compile/cons (compile/quote a) (compile/quote (b c)) nil)  ; → (a b c)
```

## Object operator specials

The forms in this section all map directly to `OBJECT_*` opcodes. They are
implemented in `koskari/compile/specials/object.py` and tested in
`tests/compile/specials/test_object_specials.py`.

### Constant folding

Reducible (COUNT) operators apply left-to-right constant folding: consecutive
leading arguments that compile to a single `CONST` are folded together using
the corresponding Python operator before any code is emitted. Folding stops at
the first non-constant argument to preserve dunder dispatch semantics.

```scheme
(compile/+ 1 2 a)   ; → CONST 3, VAR a, OBJECT_ADD 2
(compile/+ 1 2)     ; → CONST 3, OBJECT_ADD 1
(compile/+ a 1 2)   ; → VAR a, CONST 1, CONST 2, OBJECT_ADD 3  (no fold)
```

`compile/**`, `compile/instance?`, and `compile/subclass?` never fold
(non-trivial multi-argument semantics).

---

### Comparison interface

All comparison specials require at least 2 arguments and emit a COUNT opcode
with arg = number of supplied operands.

Primary symbols and aliases (see `koskari/compile/specials/object.py`):

- Equality: **`compile/==`**, alias **`compile/equal?`**
- Inequality: **`compile/!=`**, alias **`compile/not-equal?`**
- Ordering: **`compile/<`**, **`compile/<=`**, **`compile/>`**, **`compile/>=`**
  with aliases **`compile/lt?`**, **`compile/le?`**, **`compile/gt?`**, **`compile/ge?`**


| Special                  | Opcode      | Chained Python semantics                 |
| ------------------------ | ----------- | ---------------------------------------- |
| `(compile/== A B ...)`   | `OBJECT_EQ` | `A == B == ...`                          |
| `(compile/!= A B ...)`   | `OBJECT_NE` | `A != B != ...`                          |
| `(compile/< A B ...)`    | `OBJECT_LT` | `A < B < ...`                            |
| `(compile/<= A B ...)`   | `OBJECT_LE` | `A <= B <= ...`                          |
| `(compile/> A B ...)`    | `OBJECT_GT` | `A > B > ...`                            |
| `(compile/>= A B ...)`   | `OBJECT_GE` | `A >= B >= ...`                          |


Examples:

```scheme
(compile/< 0 1 2 3)      ; true  => (and (< 0 1) (< 1 2) (< 2 3))
(compile/<= 1 2 2 4)     ; true
(compile/== 5 5 7 5)     ; false
```


---

### Predicate interface

**Binary (NOARG, exactly 2 args):**


| Special                            | Opcode          | Python                   |
| ---------------------------------- | --------------- | ------------------------ |
| `(compile/in ITEM COLLECTION)`     | `OBJECT_IN`     | `ITEM in COLLECTION`     |
| `(compile/not-in ITEM COLLECTION)` | `OBJECT_NOT_IN` | `ITEM not in COLLECTION` |
| `(compile/is A B)`                 | `OBJECT_IS`     | `A is B`                 |
| `(compile/is-not A B)`             | `OBJECT_IS_NOT` | `A is not B`             |


**Type checks (COUNT, at least 2 args):**

```scheme
(compile/instance? OBJ TYPE)
(compile/instance? OBJ TYPE1 TYPE2 ...)
(compile/subclass? CLS BASE)
(compile/subclass? CLS BASE1 BASE2 ...)
```

The opcode argument encodes the number of type arguments. With a single type
the C handler pops value and type individually (arg=1). With multiple types it
collects them into a tuple via `PopTail(N)` (arg=N).


| Special               | Opcode               |
| --------------------- | -------------------- |
| `compile/instance?`   | `OBJECT_IS_INSTANCE` |
| `compile/subclass?`   | `OBJECT_IS_SUBCLASS` |


---

### Numeric interface — variadic (COUNT, constant folding)

These accept one or more arguments. The opcode arg is the total number of
values on the stack after any constant folding.


| Special                       | Opcode                   | Python fold         |
| ----------------------------- | ------------------------ | ------------------- |
| `(compile/+ A ...)`           | `OBJECT_ADD`             | `operator.add`      |
| `(compile/- A ...)`           | `OBJECT_SUBTRACT`        | `operator.sub`      |
| `(compile/* A ...)`           | `OBJECT_MULTIPLY`        | `operator.mul`      |
| `(compile/** BASE EXP [MOD])` | `OBJECT_POWER`           | no fold             |
| `(compile/& A ...)`           | `OBJECT_BIT_AND`         | `operator.and_`     |
| `(compile/| A ...)`           | `OBJECT_BIT_OR`          | `operator.or_`      |
| `(compile/^ A ...)`           | `OBJECT_BIT_XOR`         | `operator.xor`      |
| `(compile/// A ...)`          | `OBJECT_FLOOR_DIVIDE`    | `operator.floordiv` |
| `(compile// A ...)`           | `OBJECT_TRUE_DIVIDE`     | `operator.truediv`  |
| `(compile/@ A ...)`           | `OBJECT_MATRIX_MULTIPLY` | `operator.matmul`   |


---

### Numeric interface — binary (NOARG, exactly 2 args)

These always require exactly 2 arguments. The C handler ignores the opcode
arg for `BIT_LEFT`, `BIT_RIGHT`, but `MODULO` and `DIVMOD` require arg=2.


| Special                | Opcode             | Python         |
| ---------------------- | ------------------ | -------------- |
| `(compile/% A B)`      | `OBJECT_MODULO`    | `A % B`        |
| `(compile/<< A B)`     | `OBJECT_BIT_LEFT`  | `A << B`       |
| `(compile/>> A B)`     | `OBJECT_BIT_RIGHT` | `A >> B`       |
| `(compile/divmod A B)` | `OBJECT_DIVMOD`    | `divmod(A, B)` |


---

### General object interface — unary (NOARG, exactly 1 arg)

Each requires exactly one argument.


| Special                      | Opcode                                   | Python                     |
| ---------------------------- | ---------------------------------------- | -------------------------- |
| `(compile/not OBJ)`          | `OBJECT_NOT`                             | `not OBJ`                  |
| `(compile/repr OBJ)`         | `OBJECT_REPR`                            | `repr(OBJ)`                |
| `(compile/truth OBJ)`        | `OBJECT_TRUTH`                           | `bool(OBJ)`                |
| `(compile/iter OBJ)`         | `OBJECT_ITER`                            | `iter(OBJ)`                |
| `(compile/~ OBJ)`            | `OBJECT_BIT_INVERT`                      | `~OBJ`                     |
| `(compile/str OBJ ...)`      | `OBJECT_STR`                             | `str(OBJ)` / f-string join |
| `(compile/format OBJ)`       | `OBJECT_FORMAT`                          | `format(OBJ)`              |
| `(compile/format OBJ RULES)` | `OBJECT_FORMAT` or `OBJECT_FORMAT_CONST` | `format(OBJ, RULES)`       |
| `(compile/len OBJ)`          | `OBJECT_LEN`                             | `len(OBJ)`                 |
| `(compile/type OBJ)`         | `OBJECT_TYPE`                            | `type(OBJ)`                |
| `(compile/dir OBJ)`          | `OBJECT_DIR`                             | `dir(OBJ)`                 |
| `(compile/vars OBJ)`         | `OBJECT_VARS`                            | `vars(OBJ)`                |
| `(compile/aiter OBJ)`        | `OBJECT_AITER`                           | `aiter(OBJ)`               |


Note: `compile/str` is variadic (COUNT) at the C level — it uses
`KosOperator_BuildStr` / `obj_op_reducing` — useful for f-string style
multi-part concatenation. The compile-time special accepts one or more
arguments.

`compile/format` lowering matrix:

- `(compile/format OBJ)`:
  - constant `OBJ` -> folds to `CONST` string via compile-time `format`
  - non-constant `OBJ` -> emits `OBJECT_FORMAT` with arg=1
- `(compile/format OBJ RULES)`:
  - constant-normalized `RULES` (string or `(compile/quote SYMBOL)`) and
  constant `OBJ` -> folds to `CONST` string via compile-time `format`
  - constant-normalized `RULES` and non-constant `OBJ` -> emits
  `OBJECT_FORMAT_CONST`
  - dynamic `RULES` -> emits `OBJECT_FORMAT` with arg=2

Quoted-symbol convenience: `(compile/quote foo)` in RULES position is treated
as constant `"foo"` for lowering/folding decisions.

**Tests:** `tests/compile/specials/test_object_specials.py`

---

## compile/call/cc

**Symbol:** `compile/call/cc`

**Syntax:** `(compile/call/cc FN)`

**Use-case:** Undelimited call-with-current-continuation (Scheme-like). Captures
the current continuation at the call site, wraps it in a `Continuation` object,
and immediately calls `FN` with that continuation as its sole argument. When the
continuation is invoked with a value, control returns to the point just after the
`compile/call/cc` form with that value. If `FN` returns normally without invoking
the continuation, the return value of `FN` becomes the value of the form.

**Behaviour:** Emits `PUSH_CC` (opens a CC block), compiles `FN`, emits
`CAPTURE_CC` (captures the continuation and pushes it), emits `CALL 1` (calls
`FN` with the continuation), then `POP_CC` (closes the CC block on normal
return). The continuation may be invoked from within the same eval context
(direct apply) or from Python code in a frame above the owning eval loop
(upwards apply via `KoskariContinuationApply`). Cross-context invocation from
a different eval context triggers the segmented-transfer path.

**Examples:**

```scheme
;; Capture and immediately invoke: returns 42
(compile/call/cc
  (compile/lambda [k]
    (compile/call k 42)))

;; Escape continuation: short-circuit a computation
(compile/call/cc
  (compile/lambda [escape]
    (compile/begin
      (compile/call do-setup)
      (compile/call escape "early-exit")
      (compile/call do-cleanup))))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

## compile/let/cc

**Symbol:** `compile/let/cc`

**Syntax:** `(compile/let/cc K BODY ...)`

**Use-case:** Koskari-specific undelimited continuation capture. Pushes a new
child namespace, captures the current continuation and binds it to the name `K`
inside that namespace, then evaluates the body forms sequentially. If the
continuation bound to `K` is invoked later, control resumes just after the
`compile/let/cc` form with the supplied value. This is the idiomatic way to
name a continuation for later use without wrapping the body in a lambda.

**Behaviour:** Emits `PUSH_CC`, then opens a `child_namespace` that declares
`K` as a local. Inside the namespace: `CAPTURE_CC` captures the continuation
and pushes it; `VAR_STORE K` binds it to the declared name; the body is
compiled with `helper_begin`. Finally `POP_CC` closes the CC block outside the
namespace. `K` is not visible after the `compile/let/cc` form.

**Examples:**

```scheme
;; Bind escape continuation k, use it inside the body
(compile/let/cc k
  (compile/begin
    (compile/call do-step-one)
    (compile/call k "done")
    (compile/call never-reached)))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

## compile/capture/cc

**Symbol:** `compile/capture/cc`

**Syntax:** `(compile/capture/cc K BODY ...)`

**Use-case:** Lowest-level undelimited continuation capture. Like
`compile/let/cc` but binds `K` in the **current** (ambient) namespace rather
than a fresh child namespace, so `K` remains accessible after the form's CC
block closes. Useful when the continuation needs to outlive the block or be
visible in a scope that already contains `K`.

**Behaviour:** Emits `PUSH_CC`, then `CAPTURE_CC` (captures and pushes the
continuation), `VAR_STORE K` (stores into the current namespace's slot for
`K`), the body via `helper_begin`, and finally `POP_CC`.

**Examples:**

```scheme
;; Capture into ambient namespace; k accessible later
(compile/capture/cc k
  (compile/begin
    (compile/call do-work k)
    (compile/call k 0)))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

## compile/reset

**Symbol:** `compile/reset`

**Syntax:** `(compile/reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Emits `PUSH_PROMPT 0`, compiles the body with `helper_begin`,
emits `POP_PROMPT 0`.

**Examples:**

```scheme
;; (reset (+ 1 (shift k (k 5)))) => 6
(compile/reset
  (compile/call +
    1
    (compile/shift k (compile/call k 5))))
```

## compile/shift

**Symbol:** `compile/shift`

**Syntax:** `(compile/shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt and bind it to `K`, then run `BODY`. `K` is a
`DelimitedContinuation` that cannot be called from Python (only from within a
Koskari eval loop via `INVOKE_K` or `INVOKE_K_VAR`).

**Behaviour:** Emits `PUSH_SHIFT 0`, declares `K` as a local variable, emits
`VAR_STORE K` (binds the captured delimited continuation), compiles body with
`helper_begin`, emits `POP_SHIFT 0`.

**Examples:**

```scheme
;; Capture k; invoke immediately
(compile/reset
  (compile/shift k
    (compile/call k "value")))
```

## import

**Symbol:** `import`

**Syntax:**

- Plain: `(import MODULE)` — bind the root package
- Member: `(import [MODULE as: BIND])` — bind BIND to the leaf module
- Relative: `(import .MODULE)`, `(import ..MODULE)` (leading dots)

`MODULE` is an unquoted symbol whose printed name is the module name; leading
dots indicate a relative import (`level = number of dots`).

**Use-case:** Import a module and bind it in the current namespace, mirroring
Python's `import` statement.

- **Plain dotted import binds the root**, Python-style: `(import foo.bar)`
  binds `foo`, with `foo.bar` reachable as an attribute. The whole dotted
  chain is imported.
- **The bracketed member form binds the leaf**: `(import [foo.bar as: fbar])`
  binds `fbar` to the `foo.bar` submodule. The member uses the same
  `[TARGET as: BIND]` grammar as `import-from`.
- **Relative imports are always leaf-style.** `(import .foo)` binds the leaf
  `foo`. A bare relative import (dots only, no name) requires the member form
  and starts at two dots — a single lone dot is the reader's dotted-pair
  marker and is not expressible in list syntax; `from . import x` is spelled
  `(import .x)`.

Inline `as:` (`(import foo as: bar)`) is **not** supported; use the bracketed
member form.

**Opcodes emitted:** a single `IMPORT` (row `(name, level, leaf_flag,
bind_name)`) then `CONST None` (statement result). The opcode imports the
module and stores its own binding; there is no separate `VAR_BIND`.

**Implementation:** `koskari/compile/specials/imports.py` — `special_import`

**Examples:**

```scheme
;; Plain dotted import — binds 'foo' (root); foo.bar reachable
(import foo.bar)

;; Simple import — binds 'os'
(import os)

;; Member form — binds 'osp' to the leaf os.path
(import [os.path as: osp])

;; Relative import (one dot) — binds leaf 'sibling'
(import .sibling)

;; Two-dot relative import — binds leaf 'parent'
(import ..parent)
```

**Tests:** `tests/compile/specials/test_import.py`

## import-from

**Symbol:** `import-from`

**Syntax:**

- `(import-from MODULE NAME ...)` — import named attributes from MODULE
- `(import-from MODULE [NAME as: BIND] ...)` — with per-name renames
- `(import-from .MODULE NAME ...)` — relative module, named attributes
- Mix of plain symbols and `[NAME as: BIND]` forms is allowed.

`MODULE` is an unquoted symbol (same leading-dot convention as `import`).
Each `NAME` is a symbol used as both the attribute name and the binding name
unless a `[NAME as: BIND]` member overrides the binding.

**Use-case:** Import specific names from a module into the current namespace.
Mirrors Python's `from MODULE import NAME [as BIND], ...`.

**Behaviour:** Emit a single `IMPORT_FROM` (row `(name, level, attr_names,
bind_names)` with the two parallel tuples) then `CONST None`. The opcode
imports the leaf module and binds every member itself; there is no module on
the stack and no compensating POP. A member that is a not-yet-imported
submodule is imported on demand (CPython's `from pkg import sub` fallback).

**Implementation:** `koskari/compile/specials/imports.py` —
`special_import_from`

**Examples:**

```scheme
;; Single name from absolute module
(import-from os path)

;; Multiple names
(import-from os path sep getcwd)

;; With a rename
(import-from os [path as: osp] sep)

;; From relative module
(import-from .util helper format)

;; Relative with rename
(import-from .util [helper as: h])
```

**Tests:** `tests/compile/specials/test_import.py`

## import-all-from

**Symbol:** `import-all-from`

**Syntax:** `(import-all-from MODULE)`

`MODULE` follows the same dotted-name and relative-import rules as `import`.
No member list, no rename, no mixing with named imports.

**Use-case:** Bulk-bind a module's public names, mirroring Python's
`from MODULE import *`. Name selection follows CPython: the module's `__all__`
when defined (a sequence of strings; a non-string entry is a `TypeError`),
otherwise every `dir(MODULE)` name that does not start with an underscore.
Existing bindings are overwritten.

**Restriction:** module top level only. Inside a function, lambda, `let`, or
class body the compiler raises `SpecialError` ("import-all-from is only
allowed at module level"), matching Python 3.

**Opcodes emitted:** a single `IMPORT_STAR` (row `(name, level, 0, 0)`) then
`CONST None`.

**Implementation:** `koskari/compile/specials/imports.py` —
`special_import_all_from`

**Examples:**

```scheme
;; Bind all public names of os.path
(import-all-from os.path)

;; From a relative package
(import-all-from .util)
```

**Tests:** `tests/compile/specials/test_import.py`,
`tests/lib/opcode/test_import_ops.py`

## Other implemented specials

The following specials are implemented but are not given full sections above.
See the cited modules for behaviour and examples.

- **compile/with** — Single-shot context manager; `(compile/with EXPR BODY ...)`.
Emits `PUSH_WITH` / `POP_WITH`. Implementation:
`koskari/compile/specials/context.py`.
- **compile/dyn-with** — Multi-shot dynamic context manager; async enter/exit.
Emits `PUSH_DYN_WITH` / `POP_DYN_WITH`. Implementation:
`koskari/compile/specials/context.py`.

## Adding New Specials

When implementing a new special form:

1. Add a handler function in the appropriate module under
  `koskari/compile/specials/` and decorate it with `@special(symbol("name"))`.
   The specials package (`koskari/compile/specials/__init__.py`) imports and
   re-exports all handlers.
2. If the form needs parameter or formals parsing, use `gather_parameters` or
  `gather_formals` from `koskari/compile/helpers.py`; add new helpers there if
   needed. Declare form structure with **`koskari.matcher`** patterns on
   `@special(..., patterns=...)` and choose binders vs capture-only slots per
   [MATCHER.md](MATCHER.md).
3. Add the handler to the import line and `__all__` in
  `koskari/compile/specials/__init__.py`.
4. Add a section to this document: symbol, syntax, use-case, behaviour, and at
  least one simple example.
