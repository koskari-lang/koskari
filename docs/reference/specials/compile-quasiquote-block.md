# compile/quasiquote-block
**Symbol:** `compile/quasiquote-block`

**Syntax:** ``HEAD: BODY` (reader shorthand) or `(quasiquote-block BLOCK-REPR)`

**Use-case:** The block-syntax analogue of `compile/quasiquote`. Captures an
entire indented block (including nested peer blocks such as `if`/`else`) as a
`:block:` cons with `,EXPR` and `,@EXPR` unquote sites active throughout the
header and body. The primary tool for writing macros in block syntax.

**Reader shorthand:** A ``` immediately preceding a block head causes the
reader to read the entire block as a raw `:block:` cons with `,`/`,@` unquote
reader macros active for all header arguments, body forms, and nested
sub-block headers and bodies.

```koskari
`begin:
  ,@forms
```

produces:

```
(quasiquote-block (:block: (begin) ((unquote-splicing forms))))
```

Peers are represented as sibling `:block:` nodes inside the enclosing body:

```koskari
`begin:
  if ,cond:
    ,then
  else:
    ,else
```

produces:

```
(quasiquote-block
  (:block: (begin)
   ((:block: (if (unquote cond)) ((unquote then)))
    (:block: (else) ((unquote else))))))
```

**Behaviour:** Purely transformative. Rewrites to `(quasiquote
BLOCK-REPR)` for normal compiler dispatch. `compile/quasiquote` walks the
`:block:` structure and fills unquote / unquote-splicing sites; it does **not**
invoke layout handlers, so the filled structure remains `:block:` reader form
where the quasi-quote produced blocks.

For **`define-syntax`** expansions, **`koskari.syntax._finalize_macro_output`**
(applied after **`MacroTransform`** handlers and **`SyntaxTransform`** template
substitution) calls **`koskari.reader.layouts.apply_layout`** on the expanded
form using **`vars(compiler.module)`** **before** binding-symbol discovery and
the shared hygiene / **`Datum`** finalize pass. That performs depth-first
`:block:` expansion through **`layout/<head>`** handlers with peer consumption,
matching reader layout semantics. Custom expansion pipelines that bypass that
finalize step must call **`apply_layout`** (or **`apply_layouts`**) themselves
when layout-expanded source is required.

**Nesting:** `_qq_expr` recognises `compile/quasiquote-block` at depth > 0
and reconstructs it as literal data, exactly like nested `compile/quasiquote`.

**Implementation:** `koskari/compile/specials/quasiquote.py`


