# compile/datum

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/datum`

**Syntax:** Used as `(datum expr)` or `(datum expr pos)` inside macro bodies.

**Behaviour:** **`macro-rules`** expansion to **`(call datum expr [position])`**, capturing
source position for hygiene.


## koskari/bootstrap/_conditionals.ki

---

Source: [design/MACROS.md](../../design/MACROS.md)
