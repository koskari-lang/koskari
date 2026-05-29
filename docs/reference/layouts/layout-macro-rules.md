# layout/macro-rules

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/macro-rules`

**Use-case:** Block syntax for the `macro-rules` transformer within a
`define-syntax` block. Same surface as `layout/syntax-rules` (literals list and
indented rules); lowers to `(macro-rules (LIT ...) ((PATTERN) TEMPLATE) ...)`.

**Behaviour:** Same as `layout/syntax-rules` except for the emitted head. Does
not accept peers.

**Syntax:**

```
macro-rules [LIT ...]:
  PATTERN1:
    BODY1...
  PATTERN2:
    BODY2...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_macro_rules`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
