# compile/unquote

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/unquote`

**Syntax:** `,EXPR` (reader shorthand) or `(compile/unquote EXPR)`

**Use-case:** Escape from quasi-quotation. Only valid inside a
`compile/quasiquote` body; at the active nesting level (level 0) the argument
is compiled and evaluated normally, and its result is inserted at that position.

**Behaviour:** When called as a top-level special form (outside quasiquote),
raises `SpecialError`. Inside quasiquote, this form is handled directly by the
`compile/quasiquote` compiler and never reaches the `compile/unquote` handler.

**Examples:**

```scheme
`(a ,x b)    ; x is evaluated
`(a ,(+ 1 2) b)  ; (+ 1 2) is evaluated, giving 3
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
