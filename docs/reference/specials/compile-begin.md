# compile/begin
**Symbol:** `compile/begin`

**Syntax:** `(begin EXPR ...)`

**Use-case:** Sequential evaluation of expressions; useful for side effects and for running several forms in order while returning only the last value. Common building block for blocks and bodies.

**Behaviour:** Compiles each expression in order. All but the last expression are evaluated and then popped from the stack (their values are discarded). The last expression's value is left on the stack (and is the value of the begin). Empty body compiles to pushing `None` and returning it.

**Examples:**

```koskari
;; Single expression: just that value
(begin (print "hello"))

;; Multiple expressions: run all, value is the last
(begin
  (do-something)
  (compute-result))
```
