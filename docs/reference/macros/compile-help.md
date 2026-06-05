# compile/help

**Syntax:** `(help)`, `(help obj)`, `(help obj ns)`

**Behaviour:** Expands to a call to the **`help`** runtime function. **`(help)`**
looks up help for the name **`help`** in **`(globals)`**. **`(help obj)`** quotes
**`obj`** and uses **`(globals)`**. **`(help obj ns)`** passes **`obj`** and the
given namespace through unchanged.

See [help](../runtime/help.md).
