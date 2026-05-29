# Block quasiquote and `:block:` layout expansion
Block backtick reads as **`(compile/quasiquote-block (:block: …))`**, rewritten to
**`(compile/quasiquote (:block: …))`**. `define-syntax` output is finalized through
**`koskari.syntax._finalize_macro_output`**, which calls **`apply_layout`** before
hygiene (see SPECIALS.md § compile/quasiquote-block).


## koskari/bootstrap/_define_syntax.ki

