# layout/macro-match
**Symbol:** `layout/macro-match`

**Use-case:** Block syntax for the `macro-match` transformer within a
`define-syntax` block. Same rule shape as **`layout/macro-rules`**: a pattern
header (symbol or parenthesized form) then body forms.

**Behaviour:** Does not accept peers.

**Syntax:**

```
macro-match:
  HEAD ARGS...:
    BODY1...
  (complex pat):
    BODY1
    BODY2
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_macro_match`

