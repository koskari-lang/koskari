# compile/define-syntax
**Symbol:** `compile/define-syntax`

**Syntax:** Matches **`(define-syntax NAME TRANSFORMER)`** where **`TRANSFORMER`** is:

- `(syntax-rules literals rule …)` → `(compile/define name (syntax-rules name literals rule …))`
- `(syntax-match rule …)` → `(compile/define name (syntax-match name rule …))`
- `(macro-rules literals rule …)` → `(compile/define name (macro-rules name literals rule …))`
- `(macro-match rule …)` → `(compile/define name (macro-match name rule …))`
- `(special-rules literals rule …)` → `(compile/define name (special-rules name literals rule …))`
- `(special-match rule …)` → `(compile/define name (special-match name rule …))`
- `(define-syntax name docstr (form …))` — re-expands with docstring attached via `set-attr` on `__doc__`

**Layout:** default **`layout/block`** flat splice at read time.

## Related

- [compile/let-syntax](compile-let-syntax.md) — local compile-time macro for the body extent
- [compile/extend-syntax](compile-extend-syntax.md)
- [compile/inline-macro](compile-inline-macro.md)
