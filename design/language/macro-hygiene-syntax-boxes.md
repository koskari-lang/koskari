# Macro hygiene and syntax boxes

**Implemented.** Procedural and template macro expansion can carry call-site
subtrees in `koskari.syntax.Syntax` wrappers. The hygiene pass
(`koskari.hygiene.expand_template_with_hygiene`, `apply_syntax_rules_hygiene`)
treats boxed segments as opaque for renaming; `koskari.syntax.unbox_macro_with_hygiene`
runs after hygiene and splices payloads into the tree for compilation.

**Archived design note:** The original middle-ground write-up (problem statement,
diagrams, and rationale) is in
[archive/macro-hygiene-syntax-boxes.md](../archive/macro-hygiene-syntax-boxes.md).

**See also:** [SPECIALS.md](../SPECIALS.md) (macros, `compile/define-syntax`, and
related compile specials); [unified-matcher-transformer-model.md](unified-matcher-transformer-model.md)
(pipeline and RULE vs MATCH terminology); [matcher-hygiene-extensions.md](../api/matcher-hygiene-extensions.md)
(matcher metadata for hygiene discovery).
