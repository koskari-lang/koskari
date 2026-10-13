# compile/syntax

**Symbol:** `compile/syntax` (surface: **`syntax`**)

**Syntax:** `(syntax NAME TRANSFORMER)` or
`(syntax NAME DOCSTRING TRANSFORMER)`, where **TRANSFORMER** is any of
the kinds [`define-syntax`](compile-define-syntax.md) accepts.

**Behaviour:** A `special-rules` compile special. Its scope depends on
where the form appears:

- At module top level, `(syntax NAME ...)` is
  `(define-syntax NAME ...)`: the transformer is bound as
  `compile/NAME` in the module and applies to the top-level forms after
  it.
- Inside a body -- a function, a `let`, a class -- the transformer
  applies to the rest of that body only, as
  [`let-syntax`](compile-let-syntax.md) does for its explicit body.
  Nothing is stored in any namespace, and the name is gone once the
  body closes.

Inside a body, a later `syntax` with the same name shadows the earlier
one for the forms after it. To bind a transformer at the module from
inside a body, write `global syntax`; see
[compile/global](../specials/compile-global.md).

**Examples:**

At top level:

```koskari
syntax twice:
  syntax-rules []:
    case (twice x):
      * 2 x

twice 4      ;; 8
```

Inside a function body:

```koskari
def scale [n]:
  syntax twice:
    syntax-rules []:
      case (twice x):
        * 2 x
  twice (twice n)

scale 5      ;; 20
```

After `scale` closes, `twice` is not defined at the module; a later
`(twice 5)` outside the body is a call to an unbound name.

## Related

- [compile/define-syntax](compile-define-syntax.md)
- [compile/let-syntax](compile-let-syntax.md)
- [compile/extend-syntax](compile-extend-syntax.md)
- [Hygiene](../../guide/hygiene.md)
