# compile/or

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/or`

**Syntax:** `(compile/or EXPR ...)`

**Use-case:** Short-circuit disjunction. Evaluates expressions left-to-right
and returns the first truthy value, skipping the rest. If all values are falsy,
returns the last value. Zero arguments yields `False` (the identity for or).
Mirrors Python's `or` operator.

**Behaviour:** Zero args: emits `CONST False`. One arg: compiles it directly.
Multiple args: compiles each expression in order; after each non-last expression,
emits `JUMP_OR done`. `JUMP_OR` pops TOS — if it is truthy, pushes it back and
jumps to `done` (short-circuit); if falsy, discards it and falls through so the
next expression can be evaluated. The last expression is compiled without a jump
and its value remains on the stack as the result. A `LABEL done` is appended.
No `POP` is needed between terms because `JUMP_OR` already consumes TOS on
the fall-through path.

**Opcodes emitted:** `CONST False` (zero args) or `JUMP_OR` (one per non-last
term).

**Examples:**

```scheme
(compile/or)              ; → False
(compile/or 42)           ; → 42
(compile/or None False 0) ; → 0   (all falsy, last value)
(compile/or None 99 0)    ; → 99  (short-circuit at second term)
(compile/or False 42)     ; → 42
```

**Tests:** `tests/compile/specials/test_cond.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
