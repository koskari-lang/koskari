# compile/load-exception
**Symbol:** `compile/load-exception`

**Syntax:** `(load-exception)`

**Use-case:** Internal form used by try handlers. Pushes the current exception
value onto the stack (e.g. for binding with as: NAME).

**Behaviour:** Emits EXCEPTION. No arguments.

