# compile/define-syntax

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Not a compiler special.** User-facing **`(define-syntax NAME TRANSFORMER)`** is
expanded by the **`compile/define-syntax`** **macro** in
`koskari/bootstrap/_define_syntax.ki` (there is no registered `@special` for
`compile/define-syntax`). The macro rewrites to **`(compile/define NAME …)`** with a
transformer whose head is **`syntax-rules`**, **`macro-rules`**, **`syntax-match`**,
**`macro-match`**, **`special-rules`**, or **`special-match`**. Those specs are
lowered by **`compile/syntax-rules`**, **`compile/macro-rules`**,
**`compile/syntax-match`**, **`compile/macro-match`**, **`compile/special-rules`**,
and **`compile/special-match`** (below).

**Typical surface form:** **`(define-syntax NAME (syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...))`**
— the macro expands that to **`compile/define`** plus a **`syntax-rules`** spec
with **`NAME`** repeated as the first argument. Block syntax via
`layout/define-syntax` and `layout/syntax-rules` produces the same shape; see
[LAYOUTS.md](../../design/LAYOUTS.md).

**Implementation:** `koskari/bootstrap/_define_syntax.ki` (macro definition).

**See also:** [MACROS.md](../../design/MACROS.md) — **compile/define-syntax (macro)**;
[MATCHER.md](../../design/MATCHER.md) — matcher syntax, composites, and hygiene tables;
[language/unified-matcher-transformer-model.md](../../design/language/unified-matcher-transformer-model.md)
(**RULE** vs **MATCH** pattern languages; unified matcher IR, transformer
pipeline, and post-expand hygiene/unbox for `define-syntax`, `macro-rules`, and
specials).

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
