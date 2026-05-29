# compile/quote

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/quote`

**Syntax:** `(compile/quote FORM)`

**Use-case:** Prevent evaluation; FORM is produced as a literal value. For
atoms, a single CONST is emitted. For non-nil lists, the form is rewritten to
(compile/cons (compile/quote e1) (compile/quote e2) ...) and that form is
compiled so each element is quoted and combined via the CONS opcode.

**Behaviour:** One argument required. If FORM is a non-nil pair, rewrite with
`requote` to a list of quoted elements (using compile/cons and compile/quote), set
position on the result, and return that pair so the compiler re-dispatches and
compiles it. Otherwise emit CONST FORM.

**Examples:**

```scheme
(compile/quote 42)
(compile/quote x)
(compile/quote (a b c))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
