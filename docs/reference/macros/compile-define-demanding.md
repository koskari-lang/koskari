# compile/define-demanding

**Syntax:** `(define-demanding NAME FORMALS BODY ...)`

**Behaviour:** Expands to
`(define NAME (demand (function NAME FORMALS BODY ...)))`. The function
runs on the demand path: called from plain Python code with no event
loop, it returns its value rather than a coroutine. A body that must
await a Python awaitable raises `KoskariSyncViolation` there. `@demand`
above a plain `define` is the same wrap.

**Examples:**

```koskari
define-demanding shout [word]:
  (+ (word.upper) "!")
```

```python
from mymodule import shout
shout("hi")     # 'HI!'
```

**See also:** [Demand, async, and defer](../../guide/demand-async-and-defer.md),
[`define-coroutine`](compile-define-coroutine.md).
