# Block quasiquote and `:block:` layout expansion

Block backtick reads raw **`:block:`** conses and wraps them in a synthetic
**`(:block: quasiquote () (INNER))`** form. **`layout/quasiquote`** lowers that
to **`(quasiquote-block INNER)`**, which rewrites to
**`(quasiquote INNER)`** for compilation.

Module reading expands ordinary blocks through **`LayoutReader.read()`** →
**`apply_layout`** / **`apply_layouts`**. **`define-syntax`** output is
finalized through **`koskari.syntax._finalize_macro_output`**, which calls
**`apply_layout`** before hygiene (see
[compile/quasiquote-block](../specials/compile-quasiquote-block.md)).


## koskari/bootstrap/_define_syntax.ki

