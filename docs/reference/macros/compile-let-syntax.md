# compile/let-syntax

**Symbol:** `compile/let-syntax` (surface: **`let-syntax`**)

**Syntax:** **`(let-syntax NAME (syntax-rules literals rule …) body …)`**

**Behaviour:** **`special-rules`** compile special (not a source rewrite macro).
At expand time it builds the same **`(syntax-rules NAME literals rule …)`** form
**`define-syntax`** would compile, runs **`full_compile`** on that spec and
evaluates the resulting expression to obtain a **`Syntax`** transformer. The body
is compiled inside **`Compiler.macro_overlay`**, which pushes the transformer
onto a stack consulted by **`find_compiled`** before the module namespace.
Nested **`let-syntax`** shadows outer bindings. The overlay is discarded when
the body compile finishes; nothing is stored in the module namespace.

**Note:** Current bootstrap arm is **`syntax-rules`** only (unary binding).

## Example

```koskari
(let-syntax twice
  (syntax-rules []
    [(twice x) (+ x x)])
  (twice 5))
;; => 10
```

Nested shadowing:

```koskari
(let-syntax twice
  (syntax-rules []
    [(twice x) (+ x 1)])
  (let-syntax twice
    (syntax-rules []
      [(twice x) (+ x x)])
    (twice 3)))
;; => 6
```

## Related

- [compile/define-syntax](compile-define-syntax.md)
- [compile/extend-syntax](compile-extend-syntax.md)
- [compile/inline-macro](compile-inline-macro.md)
- [Macro matching and hygiene](../../guide/macros-and-hygiene.md)
