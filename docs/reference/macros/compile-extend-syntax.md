# compile/extend-syntax

**Syntax:** `(extend-syntax NAME TRANSFORMER)`, with the same transformer
heads as `syntax`, `bare-syntax` included.

**Behaviour:** Rebinds `compile/NAME` at the module to the combination
of the existing transformer and the new spec, through
[`define-global`](../specials/compile-define-global.md), so the form may
sit inside a function body and still extends the module's transformer.
The new arms exist once the form has run, and apply to forms compiled
after that, as with every transformer bound from a body. The new arms
take precedence: a base transformer can carry a generic fall-through
case and a later extension that adds a more specific case wins wherever
both match. That is the basis of open dispatch, a base
`(config (tp name) ...)` plus per-type `extend-syntax config` cases
keyed on a literal head.

A [`syntax`](compile-syntax.md) written inside a body is scoped to that
body and is never bound as `compile/NAME`, so an `extend-syntax` of it
reads an unbound name when the body runs. Give a body-scoped
transformer its arms in its own `syntax` form, or use
[`let-syntax`](compile-let-syntax.md).

The original docstring carries forward, and a docstring in the extension
is appended to it. A transformer holds at most one bare form, so
extending with `bare-syntax` replaces the existing bare arm.

Extensions compose across imports. After `import-from mod foo`,
extending `foo` in the importing module adds cases that route there
while the base rules still apply to inputs the new cases do not match.
A library can ship a base macro and let each consumer specialize it.

**Examples:**

```koskari
syntax greet:
  syntax-rules []:
    greet name:
      f"hello {name}"

extend-syntax greet:
  syntax-rules [loud]:
    greet loud name:
      f"HELLO {name}"

(greet "a")          ;; "hello a"
(greet loud "b")     ;; "HELLO b"
```

**See also:** [`syntax`](compile-syntax.md),
[`define-syntax`](compile-define-syntax.md),
[`decorate-subject`](compile-decorate-subject.md), whose shape arms are
extended this way.
