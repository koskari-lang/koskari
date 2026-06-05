# compile/setq-values
**Symbol:** `compile/setq-values`

**Syntax:** `(setq-values PATTERN VALUE)`

PATTERN is either a symbol (single store) or a list pattern in the same shape as
`compile/for-each` unpack bindings (including `*:` rest segments).

**Use-case:** Store into existing variables by unpacking the value of VALUE
according to PATTERN. Evaluates VALUE once, then assigns components (or the
whole value for a symbol pattern).

**Behaviour:** Requests write access for every binder in the pattern, compiles
VALUE, then emits unpack store bytecode via the same helper as unpack-style
`using` bindings (`declare=False`). The form evaluates to None.

**Note:** The `set!` macro expands `(set! (values pattern ...) value)` to
`(setq-values [pattern ...] value)`; see [Macros](../macros/index.md).

**Examples:**

```koskari
(setq-values [a b] (pair 1 2))
(setq-values [a *: b c] seq)

;; Via set! macro expansion
(set! (values x y) point)
```

**Implementation:** `koskari/compile/specials/variables.py` — `special_setq_values`


