# layout/syntax-match

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/syntax-match`

**Use-case:** Block syntax for the `syntax-match` transformer within a
`define-syntax` block. No literals list; each rule lowers to **`(PATTERN
TEMPLATE)`** (no **`rule`** wrapper).

**Behaviour:** Same per-rule header/body rules as `layout/syntax-rules` (including
`match (PATTERN):` for keywords). Does not accept peers.

**Syntax:**

```
syntax-match:
  PATTERN1:
    TEMPLATE1...
  match (PATTERN-WITH-KEYWORDS):
    TEMPLATE...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_match`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
