# compile/function-compose

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** `(function-compose FN)` or `(function-compose FN1 FN2 …)`

**Behaviour:** **`macro-rules`** expansion. A single **FN** is unchanged. Two or more
**FN**s expand to a **`lambda [*: args **: kwds]`** that nests calls from right to left:
the rightmost **FN** receives **`*: args **: kwds`**, and each **FN** to the left wraps
the accumulated call (same order as **`(decorate [DECORATOR …] FN)`** folding). Example
from the macro docstring:

```text
(function-compose fn1 (fn2 foo) fn3)
  ⇒ (lambda [*: args **: kwds]
       (fn1 ((fn2 foo) (fn3 *: args **: kwds))))
```

Composition is syntactic: argument expressions are not evaluated until the composed
function is invoked.

---

Source: [design/MACROS.md](../../design/MACROS.md)
