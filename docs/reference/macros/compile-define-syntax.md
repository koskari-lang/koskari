# compile/define-syntax
**Symbol:** `compile/define-syntax`

**Syntax:** Matches **`(define-syntax NAME TRANSFORMER)`** where **`TRANSFORMER`** is:

- `(syntax-rules literals rule …)` → `(define-global compile/name (syntax-rules name literals rule …))`
- `(syntax-match rule …)` → `(define-global compile/name (syntax-match name rule …))`
- `(macro-rules literals rule …)` → `(define-global compile/name (macro-rules name literals rule …))`
- `(macro-match rule …)` → `(define-global compile/name (macro-match name rule …))`
- `(special-rules literals rule …)` → `(define-global compile/name (special-rules name literals rule …))`
- `(special-match rule …)` → `(define-global compile/name (special-match name rule …))`
- `(bare-syntax slot template)` → `(define-global compile/name (bare-syntax name slot template))`
- `(define-syntax name docstr (form …))` -- re-expands with docstring appended via `append_doc`

The transformer is bound under the `compile/` prefix whatever spelling the
definition used; the written name stays the transformer's identity in help
text and match errors.

## Bare forms

A `bare-syntax` transformer kind gives the transformer a **bare form**: an
expansion for the name used as a lone atom rather than at the head of a
call form. `slot` names the called-by capture, the only binding the
template may refer to. A transformer carries at most one bare form.

A transformer that has *only* a bare form (no call arms) acts as an alias
in form position: `(name args …)` expands the head and keeps the args. A
transformer with call arms never falls back to its bare form -- a call no
arm matches is an error.

The bootstrap uses bare forms to give the statement words `return`,
`continue`, `break`, and `raise` their Python-style lone spellings; the
cost is that a name with a bare form always expands and can never name a
variable.

**Layout:** default **`layout/block`** flat splice at read time; a
`bare-syntax SLOT:` block takes the template as its body.

## Related

- [compile/let-syntax](compile-let-syntax.md) -- local compile-time macro for the body extent
- [compile/extend-syntax](compile-extend-syntax.md)
- [compile/inline-macro](compile-inline-macro.md)
