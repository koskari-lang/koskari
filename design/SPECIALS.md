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
With optional keyword args: `(compile/call CALLEE ARG ... KEY: VAL ...)`
With rest args: `(compile/call CALLEE ARG ... * REST)` or `** KW-REST`.

**Use-case:** Generic function application. Evaluates the callee and each
argument, then emits a call instruction. Supports positional arguments only in
the current VM (CALL). Keyword and star forms emit CALL_NAMES / CALL_STAR, which
are stubbed in the interpreter; calls that use keyword or rest arguments are
not yet runnable and will raise at runtime until those opcodes are implemented.

**Behaviour:** Compiles the first subform (the callee), then parses the
remaining forms with `gather_parameters` into positional, keyword, and
optional `*` / `**` rest. For positional-only calls, emits code that evaluates
each argument and then `CALL n`. Tail-call mode (`tc`) is respected when set by
the compiler.

**Examples:**

```scheme
;; Positional call (fully supported)
(compile/call add 1 2)
(compile/call my-func a b c)

;; Keyword and star forms (emit opcodes; VM support pending)
(compile/call fn 1 2 name: "x")
(compile/call apply-fn *: args)
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


## Adding New Specials

When implementing a new special form:

1. Implement the handler in `koskari/compile/specials.py` and register it with
   the `@special(symbol("name"))` decorator.
2. If the form needs parameter or formals parsing, use `gather_parameters` or
   `gather_formals` from `koskari/compile/helpers.py`; add new helpers there if
   needed.
3. Register the special in the module namespace used by the compiler (e.g. in
   tests, store each entry in `SPECIALS` under the symbol used at the call site;
   the compiler looks up the head symbol in the module).
4. Add a section to this document: symbol, syntax, use-case, behaviour, and at
   least one simple example.
