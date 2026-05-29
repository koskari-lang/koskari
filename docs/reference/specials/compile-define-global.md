# compile/define-global

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/define-global`

**Syntax:** Same arities and spellings as `compile/define`: **inline type**
(`NAME :: TYPE` / `NAME type: TYPE`) or **DECL** as the first argument:

- `(compile/define-global NAME :: TYPE)` … (and `type:`), or `(compile/define-global DECL)`
- `(compile/define-global NAME :: TYPE EXPR)` or `(compile/define-global DECL EXPR)`
- `(compile/define-global NAME :: TYPE FORMALS BODY ...)` or
`(compile/define-global DECL FORMALS BODY ...)`

See **compile/define** for the meaning of **DECL** and function-shorthand
lowering to `compile/lambda` (including `NAME :: TYPE` when a return type is
present).

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

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
