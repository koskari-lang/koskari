# compile/check-exception
**Symbol:** `compile/check-exception`

**Syntax:** `(check-exception TYPE ...)`

**Use-case:** Internal form used by compile/try. Checks if the current
exception matches any of the given types; if so, clears the exception and
continues; otherwise falls through.

**Behaviour:** Compiles each TYPE, emits EXCEPTION_MATCH with the count.
Used inside the cond built by _except.

**Examples:** Internal use only; not normally written by users.
