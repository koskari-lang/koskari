# compile/define-global
**Symbol:** `compile/define-global`

**Syntax:** Same arities and spellings as `compile/define`: **inline type**
(`NAME :: TYPE` / `NAME type: TYPE`) or **DECL** as the first argument:

- `(define-global NAME :: TYPE)` ... (and `type:`), or `(define-global DECL)`
- `(define-global NAME :: TYPE EXPR)` or `(define-global DECL EXPR)`
- `(define-global NAME :: TYPE FORMALS BODY ...)` or
`(define-global DECL FORMALS BODY ...)`

See **compile/define** for the meaning of **DECL** and function-shorthand
lowering to [compile/function](compile-function.md) (including `NAME :: TYPE`
when a return type is present). Same inner form as `define`; additionally calls
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

```koskari
;; Explicit global; setq writes to module
(define-global x 1)
(setq x 42)

;; Shared counter across nested closures
((lambda ()
   (define-global counter 0)
   (define inc (lambda []
     (setq counter (+ counter 1))
     counter))
   (inc)))
```
