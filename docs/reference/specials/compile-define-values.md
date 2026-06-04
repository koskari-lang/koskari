# compile/define-values
**Symbol:** `compile/define-values`

**Syntax:** `(define-values PATTERN VALUE)`

Same PATTERN shapes as `compile/setq-values`.

**Use-case:** Introduce new bindings and assign by unpacking the value of VALUE
according to PATTERN (parallel to `compile/define` for a single name).

**Behaviour:** Compiles VALUE, then emits unpack store bytecode with
`declare=True` for each binder (same helper as `compile/setq-values`, without
prior `request_for_write`). The form evaluates to None.

**Note:** The `let-values` macro sequences this form inside `let`; see
[Macros](../macros/index.md).

**Examples:**

```koskari
(define-values [a b] (pair 1 2))
(define-values [a *: b c] seq)

;; Via let-values macro
(let-values [(x y) point] (use x y))
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_define_values`


