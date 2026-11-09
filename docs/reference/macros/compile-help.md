# compile/help

**Syntax:** `(help)`, `(help OBJ)`, `(help OBJ NS)`

**Behaviour:** Shows help as the [`help`](../runtime/help.md) runtime
function does. `(help)` shows help for the name `help` in `(globals)`.
`(help OBJ)` takes `OBJ` as written, without evaluating it, and looks it
up in `(globals)`, trying the runtime name and its `compile/`, `layout/`,
and `match/` facets. `(help OBJ NS)` passes `OBJ` and `NS` to the
function as ordinary arguments, without quoting `OBJ`. Output goes
through the pager.

**Examples:**

```koskari
(help var)
(help if)
(help compile/lambda)
(help layout/try)
(help pragma)
(help (pragma compiler/call-brackets))
```

**See also:** [`help`](../runtime/help.md).

**Also:** function [`help`](../runtime/functions.md#compilation-and-expansion).
