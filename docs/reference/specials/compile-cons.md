# compile/cons

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/cons`

**Syntax:** `(compile/cons EXPR1 EXPR2 ... EXPR_TAIL)`

**Use-case:** Variadic pair construction. Equivalent to (cons EXPR1 (cons EXPR2
(... (cons EXPR_TAIL) ...))). At least one argument (the tail).

**Behaviour:** Compile each expression in order (stack bottom = first element,
top = tail). Emit CONS with arg = number of expressions. VM builds the chain
via KosPair_ConsFromPtr(TailPtr(block, count), count), then Drop(count) and
push result. count 0 pushes nil.

**Example:**

```scheme
(compile/cons 1 2 nil)                    ; → (1 . (2 . nil))
(compile/cons (compile/quote a) (compile/quote (b c)) nil)  ; → (a b c)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
