# compile/setq-values

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/setq-values`

**Syntax:** `(compile/setq-values PATTERN VALUE)`

PATTERN is either a symbol (single store) or a list pattern in the same shape as
`compile/for-each` unpack bindings (including `*:` rest segments).

**Use-case:** Store into existing variables by unpacking the value of VALUE
according to PATTERN. Evaluates VALUE once, then assigns components (or the
whole value for a symbol pattern).

**Behaviour:** Requests write access for every binder in the pattern, compiles
VALUE, then emits unpack store bytecode via the same helper as unpack-style
`using` bindings (`declare=False`). The form evaluates to None.

**Note:** The `set!` macro expands `(set! (values pattern ...) value)` to
`(compile/setq-values [pattern ...] value)`; see [MACROS.md](../../design/MACROS.md).

**Examples:**

```scheme
(compile/setq-values [a b] (compile/call pair 1 2))
(compile/setq-values [a *: b c] seq)

;; Via set! (see MACROS.md)
(set! (values x y) point)
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_setq_values`

**Tests:** `tests/compile/specials/test_var_specials.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
