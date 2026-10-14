# compile/define-values
**Symbol:** `compile/define-values`

`define-values` is the special that
[`var`](compile-var.md) over a pattern lowers to, and its Scheme spelling.

**Syntax:** `(define-values PATTERN VALUE)` or `(define-values PATTERN)`

Same PATTERN shapes as `compile/setq-values`: a list of names, nested
lists, and one `*:` rest collector; with a **VALUE**, a single name as
well. Without **VALUE** the pattern's names are declared and nothing is
stored; a single name without a value is [compile/var](compile-var.md).

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
