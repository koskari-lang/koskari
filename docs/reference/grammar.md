# Grammar

Koskari source combines **layout syntax** (indentation-based blocks and lines
in `.ki` files) with **s-expressions** (parenthesized forms). The reader turns
both into list structure; the compiler then expands macros and dispatches on
special forms.

---

## S-expression forms

<!-- content TBD: base s-expression grammar; see tutorial examples in guide and start tracks -->

Parenthesized calls, literals, and collection syntax work inside delimiters and
at the top level. See [Forms and combining](../../guide/forms-and-combining.md)
for everyday shapes.

---

## Layout forms

Layout is the default surface at the top level of a module. Rules for blocks,
lines, colon handling, and handler resolution live in dedicated reference pages:

- [Layout and s-expressions](concepts/layout-and-s-expressions.md) — mental model
  and three authoring surfaces
- [Reader and layout syntax](reader-layout.md) — normative syntax rules

---

## Layout handlers

Per-head read-time lowering (`layout/if`, `layout/block`, …) is documented in
the [Layout handler reference](layouts/index.md).
