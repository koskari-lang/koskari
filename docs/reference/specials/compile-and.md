# compile/and

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/and`

**Syntax:** `(compile/and EXPR ...)`

**Use-case:** Short-circuit conjunction. Evaluates expressions left-to-right
and returns the first falsy value, skipping the rest. If all values are truthy,
returns the last value. Zero arguments yields `True` (the identity for and).
Mirrors Python's `and` operator.

**Behaviour:** Zero args: emits `CONST True`. One arg: compiles it directly.
Multiple args: compiles each expression in order; after each non-last expression,
emits `JUMP_AND done`. `JUMP_AND` pops TOS — if it is falsy, pushes it back and
jumps to `done` (short-circuit); if truthy, discards it and falls through so the
next expression can be evaluated. The last expression is compiled without a jump
and its value remains on the stack as the result. A `LABEL done` is appended.
No `POP` is needed between terms because `JUMP_AND` already consumes TOS on
the fall-through path.

**Opcodes emitted:** `CONST True` (zero args) or `JUMP_AND` (one per non-last
term).

**Examples:**

```scheme
(compile/and)             ; → True
(compile/and 42)          ; → 42
(compile/and 1 2 3)       ; → 3   (all truthy, last value)
(compile/and 1 None 3)    ; → None (short-circuit at second term)
(compile/and False 99)    ; → False
```

**Tests:** `tests/compile/specials/test_cond.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
