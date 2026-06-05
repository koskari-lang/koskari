# compile/define-syntax
**Not a compiler special.** User-facing **`(define-syntax NAME TRANSFORMER)`** is
expanded by the **`compile/define-syntax`** **macro** in
`koskari/bootstrap/_define_syntax.ki` (there is no registered `@special` for
`compile/define-syntax`). The macro rewrites to **`(define NAME …)`** with a
transformer whose head is **`syntax-rules`**, **`macro-rules`**, **`syntax-match`**,
**`macro-match`**, **`special-rules`**, or **`special-match`**. Those specs are
lowered by **`compile/syntax-rules`**, **`compile/macro-rules`**,
**`compile/syntax-match`**, **`compile/macro-match`**, **`compile/special-rules`**,
and **`compile/special-match`** (below).

**Typical surface form:** **`(define-syntax NAME (syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...))`**
— the macro expands that to **`compile/define`** plus a **`syntax-rules`** spec
with **`NAME`** repeated as the first argument. Block syntax via
[layout/define-syntax](../layouts/layout-define-syntax.md) and
[layout/syntax-rules](../layouts/layout-syntax-rules.md) produces the same shape.

**Implementation:** `koskari/bootstrap/_define_syntax.ki` (macro definition).

**See also:** [Macros](../macros/index.md),
[Macro matching and hygiene](../concepts/macro-matching-and-hygiene.md),
**`compile/syntax-rules`**, **`compile/syntax-match`**.
