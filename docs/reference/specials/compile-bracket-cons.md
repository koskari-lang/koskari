# compile/bracket-cons
**Symbol:** `compile/bracket-cons`

**Syntax:** `(bracket-cons EXPR1 EXPR2 ... EXPR_TAIL)`

**Use-case:** Variadic bracket pair construction, the `[ ]` twin of
[`cons`](compile-cons.md). Equivalent to (bracket-cons EXPR1 (bracket-cons
EXPR2 (... EXPR_TAIL ...))). At least one argument (the tail). As a special
form it builds the chain whatever the enclosing scope binds under the name
`bracket-cons`; the runtime builtin of that name is the same operation as a
first-class value.

**Behaviour:** Compile each expression in order (stack bottom = first element,
top = tail). Emit BRACKET_CONS with arg = number of expressions. A keyword
among the arguments, `*:` or `**:`, makes the form a call of the runtime
builtin instead.

**Example:**

```koskari
(bracket-cons 1 2 bracket-nil)   ; -> [1 2]
(bracket-cons 1 2)               ; -> [1 . 2]
```

**See also:** [`bracket-proper`](compile-bracket-proper.md) for a proper
bracket list with the tail implied.

**Also:** function [`bracket-cons`](../runtime/functions.md#pairs).
