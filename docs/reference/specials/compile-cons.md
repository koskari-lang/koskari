# compile/cons
**Symbol:** `compile/cons`

**Syntax:** `(cons EXPR1 EXPR2 ... EXPR_TAIL)`

**Use-case:** Variadic pair construction. Equivalent to (cons EXPR1 (cons EXPR2
(... (cons EXPR_TAIL) ...))). At least one argument (the tail).

**Behaviour:** Compile each expression in order (stack bottom = first element,
top = tail). Emit CONS with arg = number of expressions. VM builds the chain
via KosPair_ConsFromPtr(TailPtr(block, count), count), then Drop(count) and
push result. count 0 pushes nil.

**Example:**

```koskari
(cons 1 2 nil)                    ; -> (1 . (2 . nil))
(cons (quote a) (quote (b c)) nil)  ; -> (a b c)
```
