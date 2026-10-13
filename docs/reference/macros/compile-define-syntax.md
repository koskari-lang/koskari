# compile/define-syntax
**Symbol:** `compile/define-syntax`

`define-syntax` is the Scheme-layer spelling of
[`syntax`](compile-syntax.md) at module top level, and of `global syntax`.

**Syntax:** Matches **`(define-syntax NAME TRANSFORMER)`** where **`TRANSFORMER`** is:

- `(syntax-rules literals rule ...)` -> `(global compile/name (syntax-rules name literals rule ...))`
- `(syntax-match rule ...)` -> `(global compile/name (syntax-match name rule ...))`
- `(macro-rules literals rule ...)` -> `(global compile/name (macro-rules name literals rule ...))`
- `(macro-match rule ...)` -> `(global compile/name (macro-match name rule ...))`
- `(special-rules literals rule ...)` -> `(global compile/name (special-rules name literals rule ...))`
- `(special-match rule ...)` -> `(global compile/name (special-match name rule ...))`
- `(bare-syntax slot template)` -> `(global compile/name (bare-syntax name slot template))`
- `(define-syntax name docstr (form ...))` -- re-expands with docstring appended via `append_doc`

The transformer is bound under the `compile/` prefix whatever spelling the
definition used; the written name stays the transformer's identity in help
text and match errors.

## Bare forms

A `bare-syntax` transformer kind gives the transformer a **bare form**: an
expansion for the name used as a lone atom rather than at the head of a
call form. `slot` names the called-by capture, the only binding the
template may refer to. A transformer carries at most one bare form.

A transformer that has *only* a bare form (no call arms) acts as an alias
in form position: `(name args ...)` expands the head and keeps the args. A
transformer with call arms never falls back to its bare form -- a call no
arm matches is an error.

The bootstrap uses bare forms to give the statement words `return`,
`continue`, `break`, and `raise` their Python-style lone spellings; the
cost is that a name with a bare form always expands and can never name a
variable.

**Layout:** default **`layout/block`** flat splice at read time; a
`bare-syntax SLOT:` block takes the template as its body. See
[`compile/bare-syntax`](../specials/compile-bare-syntax.md) and
[layout/bare-syntax](../layouts/layout-bare-syntax.md).

## Examples

```koskari
define-syntax unless:
  syntax-rules []:
    unless test body ...:
      if test:
        None
      else:
        (begin body ...)
```

```koskari
(define-syntax twice (syntax-rules [] [(twice x) (* 2 x)]))
(twice 4)      ;; 8
```

The macro and its first use are in separate top-level forms; a `begin`
that defines a macro and uses it in the same block fails, since the
transformer is bound when its defining form finishes compiling.

## Related

- [compile/let-syntax](compile-let-syntax.md) -- local compile-time macro for the body extent
- [compile/extend-syntax](compile-extend-syntax.md)
- [compile/inline-macro](compile-inline-macro.md)
- [Hygiene](../../guide/hygiene.md) -- how a template's names are kept apart
  from the use site, and when a transformer needs `#'` or `` #` ``
