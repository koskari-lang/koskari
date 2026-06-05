# compile/await
**Symbol:** `compile/await`

**Syntax:** `(await)` or `(await EXPR)`

**Use-case:** Koskari runs code under an async evaluator: ordinary Python calls that return a coroutine or awaitable are **auto-awaited** at the CALL boundary, so their final value is what you usually see on the stack. You rarely need an explicit await for those paths. `compile/await` exists for values that are **not** eagerly awaited—most importantly values produced by the **`defer`**
builtin (`defer(fn, *args, **kwds)` in Python; see [koskari.lib](../../api/koskari-lib.md) and [Demand, async, and defer](../concepts/demand-async-and-defer.md)). A defer is a single-shot await handle: the wrapped call runs at **defer construction** (without auto-await on its result), but the VM does **not** auto-await the defer itself, so you can pass the handle around and choose **when** to await delegation or a cached sync result. Use `(await EXPR)` when `EXPR` evaluates to such a defer (or any other awaitable you need to await explicitly at this program point).

With **no arguments**, `compile/await` emits a **bare** cooperative suspend (`AWAIT` with stack count `0`) and then **`CONST None`** so the expression’s value is `None`.

**Behaviour:** With one argument, compiles `EXPR` onto the stack, then emits `AWAIT` with operand **1** (one stack element). The opcode pops TOS, dispatches the awaitable, and may suspend with `KOS_EV_AWAIT` or push the resolved value. With zero arguments, emits `AWAIT` with operand **0** then `CONST None`.

**Examples:**

```koskari
;; Await a defer stored in d (defer was used so d is not auto-awaited)
(await d)

;; Cooperative suspend only; value of the form is None
(await)
```


