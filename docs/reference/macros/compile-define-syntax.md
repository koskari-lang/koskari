# compile/define-syntax

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

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

---

Source: [design/MACROS.md](../../design/MACROS.md)
