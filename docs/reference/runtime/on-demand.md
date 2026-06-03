# on-demand?

Runtime builtin (not a macro): defined in bootstrap `_conditionals.ki`.

**Behaviour:** Returns true when the current evaluation path is a **demand**
context where bare `(await)` would raise `KoskariSyncViolation`. Implemented by
trying `(await)` and catching that exception.

Use it to branch when code may run under demand import or `demand-call` as well
as under an asyncio-hosted REPL.

See [Demand, async, and defer](../concepts/demand-async-and-defer.md) and
[compile/await](../specials/compile-await.md).
