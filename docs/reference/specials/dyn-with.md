# dyn-with
**Symbol:** `compile/dyn-with`

**Syntax:** `(dyn-with [name? expr] body ...)`

Same syntax as `with`; optional `name` binds the enter result.

**Use-case:** Context manager that a continuation transfer runs (shift/reset,
undelimited call/cc), as it runs a [`dyn-wind`](compile-dyn-wind.md): a
transfer that leaves the body calls exit, and one that resumes a captured
body calls enter again before the body continues, once per resumption. A
plain [`with`](with.md) is local to its body and a transfer runs neither.
When enter raises on such a re-entry, the body was never entered: a `try`
inside it does not see the exception and exit does not run for it; the
exception reaches the call of the continuation as a `dyn-wind` before
thunk's does.

**Generators:** A `yield` in the body leaves the `dyn-with` as a transfer
does. The yield calls exit, with no exception, before the consumer has the
value, and the generator's next step calls enter again and binds `name` to
its result before the body continues. An exit that raises at a yield raises
from the `dyn-with`, and the value is not presented. The order among nested
extents, and a generator closed or dropped at such a yield, are as for a
[`dyn-wind`](compile-dyn-wind.md). A plain [`with`](with.md) calls neither
at a yield.

**Behaviour:** Same as `with` except emits PUSH_DYN_WITH and POP_DYN_WITH.
The expression value is the body's value, or `None` when the exit swallows
an exception. See
[PUSH_DYN_WITH / POP_DYN_WITH](../vm/opcodes/kos-op-push-dyn-with.md) in the VM
reference.

**Examples:**

The shift's abort runs exit, and resuming `k` runs enter again before the
body continues; that copy runs exit when it completes.

```koskari
class Mgr []:
    def __init__ [self log]:
        (set! self.log log)
        (set! self.open False)
        (set! self.seen None)
    def __enter__ [self]:
        (self.log.append "enter")
        (set! self.open True)
        self
    def __exit__ [self *: args]:
        (self.log.append "exit")
        (set! self.open False)
        (set! self.seen args[0])
        False

def dyn_with_resumes [log]:
    var r (reset
        (+ 1 (dyn-with [c (Mgr log)]
               (shift k (begin (log.append "handler") (k 1))))))
    #[r log]

(dyn_with_resumes #[])
;; => [2, ["enter", "exit", "handler", "enter", "exit"]]
```
