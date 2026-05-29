# compile/define-values

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/define-values`

**Syntax:** `(compile/define-values PATTERN VALUE)`

Same PATTERN shapes as `compile/setq-values`.

**Use-case:** Introduce new bindings and assign by unpacking the value of VALUE
according to PATTERN (parallel to `compile/define` for a single name).

**Behaviour:** Compiles VALUE, then emits unpack store bytecode with
`declare=True` for each binder (same helper as `compile/setq-values`, without
prior `request_for_write`). The form evaluates to None.

**Note:** The `let-values` macro sequences this form inside `let`; see
[MACROS.md](../../design/MACROS.md).

**Examples:**

```scheme
(compile/define-values [a b] (compile/call pair 1 2))
(compile/define-values [a *: b c] seq)

;; Via let-values (see MACROS.md)
(let-values [(x y) point] (use x y))
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_define_values`

**Tests:** `tests/compile/specials/test_var_specials.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
