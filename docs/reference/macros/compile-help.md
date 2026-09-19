# compile/help

**Syntax:** `(help)`, `(help OBJ)`, `(help OBJ NS)`

**Behaviour:** Expands to a call to the [`help`](../runtime/help.md)
runtime function. `(help)` shows help for the name `help` in
`(globals)`. `(help OBJ)` quotes `OBJ` and looks it up in `(globals)`,
trying the runtime name and its `compile/`, `layout/`, and `match/`
facets. `(help OBJ NS)` passes both through unchanged. Output goes
through the pager.

**Examples:**

```koskari
(help define)
(help if)
(help compile/lambda)
(help layout/try)
(help pragma)
(help (pragma compiler/call-brackets))
```

**See also:** [`help`](../runtime/help.md).
