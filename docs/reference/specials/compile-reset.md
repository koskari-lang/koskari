# compile/reset

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/reset`

**Syntax:** `(compile/reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Emits `PUSH_PROMPT 0`, compiles the body with `helper_begin`,
emits `POP_PROMPT 0`.

**Examples:**

```scheme
;; (reset (+ 1 (shift k (k 5)))) => 6
(compile/reset
  (compile/call +
    1
    (compile/shift k (compile/call k 5))))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
