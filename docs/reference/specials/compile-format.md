# compile/format
**Symbol:** `compile/format`

**Syntax:** `(format OBJ)` or `(format OBJ RULES)`

**Use-case:** Build a display string from `OBJ`, optionally with a format
specification (string or quoted symbol constant).

**Behaviour:** One argument uses default formatting; two arguments pass
`RULES` to Python `format`. Constant cases may fold at compile time.

**Examples:**

```koskari
(format 42)
(format 255 "{:x}")
```

See [Object operator overview](object-operators/index.md).
