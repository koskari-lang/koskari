# compile/bracket-proper
**Symbol:** `compile/bracket-proper`

**Syntax:** `(bracket-proper EXPR ...)`

**Use-case:** A proper bracket list of the values, bracket-nil as its tail,
the way a `[ ]` literal in a quasiquote is built. No arguments is
bracket-nil. As a special form it builds the list whatever the enclosing
scope binds under the name.

**Behaviour:** Compile each expression in order, then emit BRACKET_PROPER
with arg = number of expressions. The quasiquote compiler emits the same
opcode for the segments of a `[ ]` form.

**Example:**

```koskari
(bracket-proper 1 (+ 1 1) 3)   ; -> [1 2 3]
(bracket-proper)               ; -> bracket-nil
```

**See also:** [`bracket-cons`](compile-bracket-cons.md) for an explicit
tail.
