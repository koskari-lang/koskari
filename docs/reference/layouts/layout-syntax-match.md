# layout/syntax-match
**Symbol:** `layout/syntax-match`

**Use-case:** Block syntax for the `syntax-match` transformer within a
`define-syntax` block. No literals list; each rule lowers to **`(PATTERN
TEMPLATE)`** (no **`rule`** wrapper).

**Behaviour:** Same per-rule header/body rules as `layout/syntax-rules` (including
`case (PATTERN):` to wrap a parenthesized pattern). Does not accept peers.

**Syntax:**

```
syntax-match:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN):
    TEMPLATE...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_match`

