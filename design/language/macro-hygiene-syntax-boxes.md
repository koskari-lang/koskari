# Macro hygiene and syntax boxes

**Implemented.** Procedural and template macro expansion can carry call-site
subtrees in **`koskari.lib.Datum`** wrappers (the hygiene “syntax box” type).
Macro/syntax expansion finishes through **`koskari.syntax._finalize_macro_output`**,
which applies **`koskari.reader.layouts.apply_layout`** (module namespace) before
**`koskari.hygiene.collect_binding_symbols`** and the hygiene passes below.
The hygiene pass (**`koskari.hygiene.expand_template_with_hygiene`**,
**`apply_syntax_rules_hygiene`**) treats boxed segments as opaque for renaming;
**`finalize_macro_hygiene_unbox_one_pass`** in **`hygiene`** peels **`Datum`**
for compilation without gensymming substituted captures inside **`Datum`** cells.

**Archived design note:** The original middle-ground write-up (problem statement,
diagrams, and rationale) is in
[archive/macro-hygiene-syntax-boxes.md](../archive/macro-hygiene-syntax-boxes.md).

**See also:** [SPECIALS.md](../SPECIALS.md) (macros, `compile/define-syntax`, and
related compile specials); [unified-matcher-transformer-model.md](unified-matcher-transformer-model.md)
(pipeline and RULE vs MATCH terminology); [matcher-hygiene-extensions.md](../api/matcher-hygiene-extensions.md)
(matcher metadata for hygiene discovery).
