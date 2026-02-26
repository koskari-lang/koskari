# Koskari Special Forms — Implemented Reference

This document describes the special forms currently implemented in the Koskari
compiler. It is the reference for use-cases, syntax, and examples when adding or
changing specials. For a broader catalog of planned forms (from python-sibilant
and sibilant-ceval), see [language/special-forms.md](language/special-forms.md).

Specials are registered in the compiler's module namespace; the compiler
dispatches on the head symbol of a list form. The forms below are implemented in
`koskari/compile/specials.py` and use helpers from `koskari/compile/helpers.py`
where applicable.


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

| Form        | Syntax        | Example                | Pythonic equivalent   |
| ----------- | ------------- | ---------------------- | --------------------- |
| Positional  | `EXPR ...`    | `(f a b c)`            | `f(a, b, c)`          |
| Keyword     | `KEY: VAL`    | `(f x: 1 y: 2)`        | `f(x=1, y=2)`         |
| Star args   | `*: EXPR`     | `(f *: more)`          | `f(*more)`            |
| Star kwargs | `**: EXPR`    | `(f **: kwargs)`       | `f(**kwargs)`         |

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

**Behaviour:** Compiles the first subform (the callee), then parses the
remaining forms with `gather_parameters_ng` into positionals and star-args
(interleaved) and keywords and starstar-args (interleaved). Positional-only
calls emit `CALL n`; keyword calls emit `CALL_NAMES`; any star or starstar use
emits `CALL_STAR`. Tail-call mode (`tc`) is respected when set by the compiler.

**Examples:**

```scheme
;; Positional
(compile/call add 1 2)
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
```


## compile/lambda

**Symbol:** `compile/lambda`

**Syntax:**
- Anonymous: `(compile/lambda FORMALS BODY ...)`
- Named: `(compile/lambda NAME FORMALS BODY ...)`

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b c: default-c]`, or a single symbol
for rest. Supports `*` and `**` in formals (see `gather_formals` in helpers).

**Use-case:** Create an anonymous or named function value. Named lambdas bind the
given name in the function's scope so the function can refer to itself (e.g. for
recursion).

**Behaviour:** Parses formals with `gather_formals`, builds a fragment that
declares the parameter names, compiles the body with `helper_begin`, and
completes it as a function. Pushes a `FunctionSpec` constant and emits
`BUILD_FUNCTION`. Default values for keyword parameters are compiled in the
enclosing scope and passed in via the constant tuple for defaulted keywords.

**Examples:**

```scheme
;; Anonymous identity
(compile/lambda [x] x)

;; Anonymous two-argument function
(compile/lambda [a b] (compile/call add a b))

;; Named lambda for recursion
(compile/lambda fact [n]
  (compile/call fact ...))

;; Keyword parameter with default
(compile/lambda [x flag: true] ...)
```


## compile/let

**Symbol:** `compile/let`

**Syntax:**
- Unnamed: `(compile/let BINDINGS BODY ...)`
- Named: `(compile/let NAME BINDINGS BODY ...)`

**Bindings:** A list of binding forms; each is `[SYM EXPR]` or `(EXPR as: SYM)`.
Parsed with `gather_bindings` (see helpers).

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


## compile/if

**Symbol:** `compile/if`

**Syntax:** `(compile/if COND THEN ELSE)` or `(compile/if COND then: THEN else: ELSE)`

**Use-case:** Binary conditional. Expands to cond.

**Behaviour:** Parses condition, then, and else (positional or keyword). Builds
`(compile/cond (COND THEN) (else: ELSE))` and compiles that.

**Examples:**

```scheme
(compile/if True 1 2)    ; → 1
(compile/if False 1 2)   ; → 2
```


## compile/when

**Symbol:** `compile/when`

**Syntax:** `(compile/when COND BODY...)`

**Use-case:** Conditional with no else; yields `None` when condition is false.
Expands to cond.

**Behaviour:** Builds `(compile/cond (COND BODY...) (else: None))` and compiles.

**Examples:**

```scheme
(compile/when True 42)   ; → 42
(compile/when False 42)  ; → None
```


## compile/unless

**Symbol:** `compile/unless`

**Syntax:** `(compile/unless COND BODY...)`

**Use-case:** Execute body when condition is false; when true, yields `None`.
Expands to cond (negated test).

**Behaviour:** Builds `(compile/cond (COND None) (else: BODY...))` and compiles.

**Examples:**

```scheme
(compile/unless False 42)  ; → 42
(compile/unless True 42)   ; → None
```


## compile/try

**Symbol:** `compile/try`

**Syntax:** `(try BODY [except: TYPE BODY...] [except: TYPE as: NAME BODY...] [(TYPE) BODY...] [(TYPE as: NAME) BODY...] [else: BODY...] [finally: BODY...])`

**Use-case:** Exception handling. Runs BODY; on exception, matches against
clauses and runs the first matching handler. Optional `else:` runs when no
exception occurred. Optional `finally:` always runs (on normal exit or
exception). At least one `except:` or `finally:` is required.

**Clause forms:**
- `(except: TYPE BODY...)` — catch TYPE (or subclass); no binding
- `(except: TYPE as: NAME BODY...)` — bind caught value to NAME
- `((TYPE) BODY...)` — same as except: with no binding
- `((TYPE as: NAME) BODY...)` — same as except: with binding
- `(else: BODY...)` — run when try body completed without exception
- `(finally: BODY...)` — always run; must be last

**Behaviour:** Uses PUSH_TRY, CLEAR_TRY, POP_TRY, PUSH_FINALLY, POP_FINALLY.
Exception handlers use EXCEPTION_MATCH, EXCEPTION_CLEAR, EXCEPTION opcodes.
Builds a cond over check-exception clauses; uncaught exceptions re-raise via
compile/raise. Else runs between CLEAR_TRY and POP_TRY; finally runs after
cleanup.

**Examples:**

```scheme
;; Try with except
(try (risky-call)
  (except: Exception (handle-exception)))

;; Named binding
(try (parse input)
  ((ValueError as: e) (compile/call log e)))

;; Try/else/finally
(try (do-work)
  (else: (success))
  (finally: (cleanup)))
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

**Syntax:** `(compile/while TEST BODY...)`

**Use-case:** Loop that runs body while TEST is truthy. Supports `compile/break`
and `compile/continue` inside the body.

**Behaviour:** Uses `child_loop` to emit PUSH_LOOP (break target after POP_LOOP),
loop head label, test, JUMP_FALSE to exit, body, JUMP to head, LABEL exit,
CLEAR_LOOP. The first instruction after PUSH_LOOP is the continue target.
Evaluates to the loop result (from break, continue, or None on normal exit).

**Examples:**

```scheme
;; Simple loop
(compile/while x
  (compile/call append c x)
  (compile/setq x (compile/call sub x 1)))

;; With break
(compile/while True
  (compile/cond [done (compile/break result)]
                [else: (compile/call step)]))
```


## compile/for-each

**Symbol:** `compile/for-each`

**Syntax:** `(compile/for-each [BINDING EXPR] BODY... [else: ELSEBODY])`

**Use-case:** Iterate over the sequence produced by EXPR, binding each element
to BINDING and evaluating BODY on each iteration. Supports `compile/break` and
`compile/continue` inside the body. An optional `[else: ELSEBODY]` clause runs
only when the iterator is exhausted naturally (i.e. not via `compile/break`);
its value becomes the loop's expression value. Without an else clause the loop
evaluates to `None`.

**Behaviour:** Emits CONST None (default loop result), compiles EXPR (the
iterable), then PUSH_FOREACH whose break target is set to a label after
POP_LOOP. The continue target (first instruction after PUSH_FOREACH) is
ITER_NEXT. On each iteration, ITER_NEXT pushes the next value or jumps to the
else label on exhaustion. The value is stored with VAR_STORE into the binding
name. The body is compiled with `helper_begin`; CONTINUE 0 resets the stack
block and jumps back to ITER_NEXT.

On exhaustion, CLEAR_LOOP marks natural exit. When an else body is present, its
result is compiled and then BREAK 1 is emitted: this pops the FOREACH region,
pushes the else result directly to the parent stack, and jumps to after_pop —
so the else value arrives cleanly without any temporary variable. When there is
no else body, POP_LOOP falls through to after_pop and pushes the region's None.
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
    [(compile/call gt i 4) (compile/break)]
    [else: (compile/call append result i)])
  [else: "finished"])

;; Break with a value
(compile/for-each [i items]
  (compile/cond
    [(compile/call eq i target) (compile/break "found")]
    [else: None]))
```

**Tests:** `tests/compile/specials/test_for_each.py`


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


## with

**Symbol:** `with`

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

**Symbol:** `dyn-with`

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

**Examples:**

```scheme
(compile/setq x 42)
(compile/setq counter (compile/call + counter 1))
```


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

**Syntax:**
- Declaration only: `(compile/define X)`
- Declare and store: `(compile/define X EXPR)`
- Function shorthand: `(compile/define X [Y Z] BODY ...)`

**Use-case:** Variable and function definition for module-level loading.
Declaration-only leaves the ref in scope but empty (reading before assignment
raises). Declare-and-store evaluates EXPR and stores the result in the slot
(equivalent to compile/bindq). Function shorthand expands to
`(define X (lambda X [Y Z] BODY ...))`, storing a named function in the slot.

**Behaviour:** Declares the variable name. For declaration-only: emits CONST
None. For declare-and-store: compiles EXPR, emits VAR_BIND to store the value,
then CONST None. For function shorthand (second element is a pair and a body
follows): builds `(compile/lambda X FORMALS BODY...)` and compiles it as the
value expression, then emits VAR_BIND to store it.

**Examples:**

```scheme
;; Declaration only; assign later with compile/setq
(compile/define x)
(compile/setq x 42)

;; Declare and store
(compile/define y 99)

;; Function shorthand
(compile/define double [x] (* x 2))
(compile/define fact [n]
  (compile/cond [(eq n 0) 1]
                [else: (* n (fact (sub n 1)))]))
```

**Tests:** `tests/compile/specials/test_define.py`


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


## compile/car

**Symbol:** `compile/car`

**Syntax:** `(compile/car EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the car of that pair. One argument.

**Behaviour:** Compile EXPR, then emit CAR. At runtime, CAR pops one value; if
it is not a pair or is nil, TypeError is raised. Otherwise the car (INCREF) is
pushed.

**Example:**

```scheme
(compile/car (compile/cons 1 nil))   ; → 1
```


## compile/cdr

**Symbol:** `compile/cdr`

**Syntax:** `(compile/cdr EXPR)`

**Use-case:** Compile code that evaluates EXPR (must produce a pair), then
pushes the cdr of that pair. One argument.

**Behaviour:** Compile EXPR, then emit CDR. At runtime, CDR pops one value; if
it is not a pair or is nil, TypeError is raised. Otherwise the cdr (INCREF)
is pushed.

**Example:**

```scheme
(compile/cdr (compile/cons 1 (compile/cons 2 nil)))   ; → (2 . nil)
```


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


## Adding New Specials

When implementing a new special form:

1. Add a handler function in the appropriate module under
   `koskari/compile/specials/` and decorate it with `@special(symbol("name"))`.
   The specials package (`koskari/compile/specials/__init__.py`) imports and
   re-exports all handlers.
2. If the form needs parameter or formals parsing, use `gather_parameters` or
   `gather_formals` from `koskari/compile/helpers.py`; add new helpers there if
   needed.
3. Add the handler to the import line and `__all__` in
   `koskari/compile/specials/__init__.py`.
4. Add a section to this document: symbol, syntax, use-case, behaviour, and at
   least one simple example.
