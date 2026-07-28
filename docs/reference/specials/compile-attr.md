# compile/attr

**Symbol:** `compile/attr`

**Syntax:** `(attr OBJ SYM)`

**Use-case:** Load the attribute named SYM from OBJ (getattr). The value is pushed on the stack.

**Behaviour:** Compiles OBJ, then emits ATTR with the attribute name (string).

**Note:** The reader produces this form from dotted syntax, so it is rarely
written by hand. A dot inside a single name (`obj.value`) and a dot attached to
any already-read value (`(get-fn x).baz`, `"".join`) both read as `compile/attr`,
left-associative for chains. See [Grammar](../grammar.md#suffix-syntax); the
`reader/dot-attrs` setting governs both spellings.

**Examples:**

```koskari
(attr obj "value")
(attr thing count)

;; the same thing, as the reader spells it
obj.value
(get-fn x).baz
```

