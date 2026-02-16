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
With optional keyword args: `(compile/call CALLEE ARG ... :KEY VAL ...)`  
With rest args: `(compile/call CALLEE ARG ... * REST)` or `** KW-REST`.

**Use-case:** Generic function application. Evaluates the callee and each
argument, then emits a call instruction. Supports positional arguments only in
the current VM (CALL); keyword and star forms emit CALL_NAMES / CALL_STAR, which
are stubbed in the interpreter until implemented.

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
(compile/call fn 1 2 :name "x")
(compile/call apply-fn * args)
```


## lambda

**Symbol:** `lambda`

**Syntax:**  
- Anonymous: `(lambda FORMALS BODY ...)`  
- Named: `(lambda NAME FORMALS BODY ...)`

**Formals:** A list or improper list of parameter names, optionally with keyword
parameters and defaults, e.g. `[a b]`, `[a b :c default-c]`, or a single symbol
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
(lambda [x] x)

;; Anonymous two-argument function
(lambda [a b] (compile/call add a b))

;; Named lambda for recursion
(lambda fact [n]
  (compile/call cond ...))

;; Keyword parameter with default
(lambda [x :flag true] ...)
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
