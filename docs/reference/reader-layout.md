# Reader and layout syntax

Normative rules for how the Koskari layout reader turns source text into form
trees. For motivation and the relationship between layout and s-expressions,
see [Layout and s-expressions](concepts/layout-and-s-expressions.md).

---

## Overview

<!-- content TBD: one-line purpose of layout mode in .ki modules -->

---

## Layout mode and s-expression mode

<!-- content TBD: depth 0 vs inside (, [, {; when each mode applies -->

---

## Logical lines and delimiter depth

<!-- content TBD: physical newline ends a logical line only at delimiter depth 0 -->

---

## Colon in layout mode

<!-- content TBD: structural terminator at depth 0 vs keyword atoms in headers -->

---

## Layout blocks

<!-- content TBD: header, structural :, mandatory newline, indented body -->

---

## Peer blocks

<!-- content TBD: same indent, declared peers, blank and comment-only lines -->

---

## Layout lines

<!-- content TBD: single form vs multi-form fold; layout_implicit_calls -->

---

## Layout handler resolution

<!-- content TBD: layout/<head>, default layout/block, layout/flat, Ki aliases -->

---

## Pragmas

<!-- content TBD: layout_enabled, layout_implicit_calls -->

---

## Formal grammar

<!-- content TBD: EBNF appendix -->

---

## See also

- [Layout handler reference](layouts/index.md) — per-head lowering (`layout/if`, …)
- [Grammar](grammar.md) — syntax hub
- [Conditionals corpus](../corpus/records/conditionals.jsonl) — verified examples across layout block, layout line, and s-expression surfaces
