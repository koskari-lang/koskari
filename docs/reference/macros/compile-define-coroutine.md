# compile/define-coroutine

**Syntax:** `(define-coroutine NAME FORMALS BODY ...)`

**Behaviour:** Expands to
`(var NAME (coroutine-fn (function NAME FORMALS BODY ...)))`. The
result is a function that `asyncio.iscoroutinefunction` accepts: when
Python calls it, it returns a coroutine. Called from Koskari it is
awaited at the call like any other function, so hand the function
itself to an asyncio API that wants a coroutine function, and do not
call it first.

**Examples:**

```koskari
import asyncio

define-coroutine work [x]:
  (await (asyncio.sleep 0))
  x

(asyncio.iscoroutinefunction work)   ;; True
(work 3)                              ;; 3, awaited here
```

**See also:** [`define-demanding`](compile-define-demanding.md),
[Demand, async, and defer](../../guide/demand-async-and-defer.md#choosing-a-construct).
