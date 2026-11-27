# Dynamic extents

An extent is the time during which a body is running. A form that owns
an extent has something to do when the body is entered and something to
do when it is left: release a lock, restore a setting, close a file.

Koskari has two families of such forms. They differ in which ways of
leaving and entering the body they respond to.

| Form | Entering | Leaving | Family |
|------|----------|---------|--------|
| [`try`](../reference/specials/compile-try.md) with `finally:` | nothing | runs the `finally:` body | local |
| [`with`](../reference/specials/with.md) | calls `__enter__` | calls `__exit__` | local |
| [`dyn-wind`](../reference/specials/compile-dyn-wind.md) | runs the before segment | runs the after segment | dynamic |
| [`dyn-with`](../reference/specials/dyn-with.md) | calls `__enter__` | calls `__exit__` | dynamic |
| [`parameterize`](../reference/specials/compile-parameterize.md) | binds the parameters | restores them | dynamic |

The local forms are Python's. They respond to the exits Python has: the
body completing, an exception, and a `return`, `break` or `continue`
that crosses the form. The dynamic forms respond to those and to the two
that Python's forms do not see: a continuation transfer, and a `yield`.

| The body is left or entered by | Local forms | Dynamic forms |
|--------------------------------|-------------|---------------|
| completion, an exception, `return`, `break`, `continue` | run | run |
| a continuation transfer out | do not run | run |
| a continuation transfer back in | do not run | run |
| a `yield`, and the step that resumes it | do not run | run |
| an `await` that suspends | do not run | do not run |

An `await` leaves nothing. A function suspended on one is still inside
every extent it had entered, and resumes inside them.

## Local exits

On a local exit the two families behave alike. A `dyn-wind` runs its
before segment, its body, and its after segment, and an exception from
the body passes through the after segment on its way out:

```koskari
def local-exit []:
    var log #[]
    try:
        dyn-wind:
            (log.append "body")
            (raise (ValueError "stop"))
        before:
            (log.append "before")
        after:
            (log.append "after")
    except [ValueError]:
        (log.append "caught")
    log

(local-exit)
;; => ["before", "body", "after", "caught"]
```

## Continuation transfers

Calling a continuation moves control from one place in the program to
another without returning through the code between them. The dynamic
extents between the two places are left and entered as the transfer
crosses them:

- extents the transfer leaves run their leaving action innermost first;
- extents the transfer enters run their entering action outermost first;
- each action runs with the extents outside its own form in place and
  nothing from its body.

An escape through two nested `dyn-wind` forms leaves the inner one, then
the outer one:

```koskari
def escape []:
    var log #[]
    call/cc:
        lambda [k]:
            dyn-wind:
                dyn-wind:
                    (k None)
                before:
                    (log.append "inner before")
                after:
                    (log.append "inner after")
            before:
                (log.append "outer before")
            after:
                (log.append "outer after")
    log

(escape)
;; => ["outer before", "inner before", "inner after", "outer after"]
```

A delimited continuation can be called more than once, and each call
enters a copy of the captured region. The `shift` below leaves the
`dyn-wind`, and each of the two calls of `k` enters it and leaves it
again:

```koskari
def twice []:
    var log #[]
    var r:
        reset:
            dyn-wind:
                (* 10 (shift k (+ (k 1) (k 2))))
            before:
                (log.append "before")
            after:
                (log.append "after")
    #[r log]

(twice)
;; => [30, ["before", "after", "before", "after", "before", "after"]]
```

When an entering action raises during a transfer, its body was never
entered: nothing inside the form sees the exception, and the form's own
leaving action does not run. The extents the transfer had already
entered are left as for any exception, and the exception reaches the
call of the continuation.

A continuation cannot be captured from inside an entering or leaving
action while it runs, and a `yield` cannot be written in a `dyn-wind`
segment. [`call/cc`](../reference/specials/compile-call-cc.md) and
[`shift`](../reference/specials/compile-shift.md) list the errors.

## Mixing Python's extents with dynamic extents

### `try` and `with` written in Koskari

A `finally:` body and a plain `with` are passed over by a transfer. The
examples use a manager that records its calls:

```koskari
class Noted []:
    def __init__ [self log name]:
        (set! self.log log)
        (set! self.name name)
    def __enter__ [self]:
        (self.log.append (+ self.name " enter"))
        self
    def __exit__ [self *: args]:
        (self.log.append (+ self.name " exit"))
        False
```

An escape through a `dyn-with`, a `with` and a `try` exits the
`dyn-with` only. The `with` manager stays entered and the `finally:`
body never runs:

```koskari
def escape-mixed []:
    var log #[]
    call/cc:
        lambda [k]:
            dyn-with [a (Noted log "dyn-with")]:
                with [b (Noted log "with")]:
                    try:
                        (k None)
                    finally:
                        (log.append "finally")
    log

(escape-mixed)
;; => ["dyn-with enter", "with enter", "dyn-with exit"]
```

A region that is entered again through its continuation holds copies of
its local forms. A copy is not entered a second time, since the transfer
runs nothing for it, but it does run its leaving action when the copy
exits locally. Below, the `shift` leaves the `dyn-wind` and passes over
the `with` and the `try`. The call of `k` enters the `dyn-wind` again,
and when the copy of the body completes, its `finally:` and its `with`
exit run on the way out:

```koskari
def resume-mixed []:
    var log #[]
    reset:
        dyn-wind:
            with [b (Noted log "with")]:
                try:
                    shift k:
                        (log.append "handler")
                        (k None)
                finally:
                    (log.append "finally")
        before:
            (log.append "before")
        after:
            (log.append "after")
    log

(resume-mixed)
;; => ["before", "with enter", "after", "handler",
;;     "before", "finally", "with exit", "after"]
```

`__enter__` ran once and `__exit__` ran once, but not around the same
stretch of the program: the manager was entered while the handler ran.
A continuation called twice would run the `finally:` and the exit twice
against one enter, and one never called would run neither.

Choose by what the cleanup protects:

- a resource that must be released whenever control leaves the body, by
  any route, belongs in a `dyn-with` or a `dyn-wind`;
- a `with` or a `finally:` is enough in code that no continuation
  crosses, and it costs less;
- inside a `reset` whose continuation may be called more than once, a
  plain `with` holds one manager for every copy of its body, and a
  `dyn-with` enters the manager once per copy.

### Python code between two Koskari extents

Python code that Koskari calls has its own `with` blocks and `finally:`
clauses, and it may call back into Koskari. Two facts decide how those
blocks line up with the Koskari extents around them.

A Koskari function called from Python returns an awaitable. Python code
that returns that awaitable has left its own blocks before the function
body runs:

```python
def guarded(log, fn):
    log.append("python enter")
    try:
        return fn()
    finally:
        log.append("python finally")
```

```koskari
def not-demanded []:
    var log #[]
    helpers.guarded log:
        lambda []:
            (log.append "body")
    log

(not-demanded)
;; => ["python enter", "python finally", "body"]
```

Wrapping the function in
[`demand`](demand-async-and-defer.md#demand-evaluation) makes the call
from Python run the body to completion, inside the Python block. The
body is then a nested evaluation: Koskari code, called by Python code,
called by Koskari code.

An exception raised in a nested evaluation leaves it as any exception
does. It passes through the Python frames, where an `except` clause may
handle it and each `finally:` and `with` runs, and then carries on
through the outer Koskari extents.

A `call/cc` continuation called in a nested evaluation leaves that
evaluation too. The dynamic extents inside it are left innermost first,
then the Python blocks between are unwound, then the transfer continues
through the outer evaluation's extents to the place the continuation
was captured:

```koskari
def through-python []:
    var log #[]
    call/cc:
        lambda [k]:
            helpers.guarded log:
                demand:
                    lambda []:
                        dyn-wind:
                            (k None)
                        before:
                            (log.append "before")
                        after:
                            (log.append "after")
    log

(through-python)
;; => ["python enter", "before", "after", "python finally"]
```

The transfer crosses the Python frames as an exception that is not an
`Exception`: an `except Exception:` clause does not see it, and a
`finally:` clause or a `with` block's exit runs as it passes. Python
code that catches `BaseException` and does not raise it again stops the
transfer, and the continuation's call returns to that Python code as a
handled exception would.

A delimited continuation does not reach across Python code. A `shift`
in a nested evaluation looks for its `reset` in that evaluation only,
and raises `KoskariPromptNotFound` when the `reset` is on the far side
of the Python frames. The continuation a `shift` captures cannot be
called from Python.

Python code can open a `parameterize` extent of its own with
`koskari.types.parameterize`;
[`parameterize`](../reference/specials/compile-parameterize.md)
describes it.

## Dynamic extents in generators

A generator's body does not run while the generator is suspended at a
`yield`, so a `yield` leaves the dynamic extents open around it, and the
step that resumes the generator enters them again. The value reaches the
consumer after the leaving actions have run.

```koskari
def counted [log]:
    dyn-wind:
        (yield 1)
        (yield 2)
    before:
        (log.append "in")
    after:
        (log.append "out")

def steps []:
    var log #[]
    for-each [x (counted log)]:
        (log.append x)
    log

(steps)
;; => ["in", "out", 1, "in", "out", 2, "in", "out"]
```

The before segment runs three times: when the body first reaches the
`dyn-wind`, and at each of the two steps that resume a `yield`. The
order among nested extents is a transfer's: a `yield` leaves innermost
first, and a step enters outermost first.

The local forms are passed over, as they are by a transfer. A `with`
stays entered across every `yield` in its body, and a `finally:` runs
when the generator's body leaves the `try`:

```koskari
def mixed [log]:
    dyn-with [a (Noted log "dyn-with")]:
        with [b (Noted log "with")]:
            try:
                (yield 1)
                (yield 2)
            finally:
                (log.append "finally")

def mixed-steps []:
    var log #[]
    for-each [x (mixed log)]:
        (log.append x)
    log

(mixed-steps)
;; => ["dyn-with enter", "with enter", "dyn-with exit", 1,
;;     "dyn-with enter", "dyn-with exit", 2,
;;     "dyn-with enter", "finally", "with exit", "dyn-with exit"]
```

A plain `with` in a generator is Python's behaviour: the manager is held
while the consumer runs. A `dyn-with` gives the resource back at each
`yield` and takes it again at each step.

### The consumer's extents

A generator's extents and its consumer's are separate. The consumer's
extents stay entered while it steps the generator, and the generator's
are entered and left inside them at each step:

```koskari
def nested []:
    var log #[]
    dyn-wind:
        for-each [x (counted log)]:
            (log.append x)
    before:
        (log.append "consumer in")
    after:
        (log.append "consumer out")
    log

(nested)
;; => ["consumer in", "in", "out", 1, "in", "out", 2,
;;     "in", "out", "consumer out"]
```

A `parameterize` in a generator is in effect for the generator's body
and not for its consumer: a `yield` takes the bindings out, and the next
step puts them back. A read in the generator of a parameter it has not
bound itself sees the binding of whoever is making the current step.

Each entering and leaving action runs in the extent of its own form, as
on a transfer. A `dyn-wind` segment does not see a `parameterize`
written inside that `dyn-wind`. It sees one written around the
`dyn-wind` in the generator, and for a parameter the generator has not
bound there, the binding of the code making the current step.

### Who steps the generator

Every step enters the extents, whatever makes it: a `for-each`, `next`,
a send, a throw, a `yield-from` in another generator, or Python code.

```python
def collect(gen):
    return [value for value in gen]
```

```koskari
def python-steps []:
    var log #[]
    var seen (helpers.collect (counted log))
    #[seen log]

(python-steps)
;; => [[1, 2], ["in", "out", "in", "out", "in", "out"]]
```

A Python generator that steps a Koskari generator keeps its own `with`
blocks entered across its own `yield` statements, as Python does, while
the Koskari generator's dynamic extents are left and entered at each
step.

### Closing, throwing and dropping

A generator closed at a `yield` inside a dynamic extent is entered, has
`GeneratorExit` raised at the `yield`, and is left, so each action runs
once more:

```koskari
def closed []:
    var log #[]
    var g (counted log)
    (log.append (next g))
    (g.aclose)
    log

(closed)
;; => ["in", "out", 1, "in", "out"]
```

A throw enters the extents and then raises at the `yield`. A generator
dropped at such a `yield` is closed the same way, when Python finalizes
it: at once where no event loop is running, and by the loop's
asynchronous-generator hook where one is. An exception from an action
run during finalization has nowhere to go and is reported as Python
reports any exception in a finalizer.

### Actions that raise

A leaving action that raises at a `yield` raises from its own form. A
`try` in the body does not see the exception, one around the form does,
and the value is not handed to the consumer. The action does not run a
second time for that exit.

An entering action that raises at a step behaves as it does on a
transfer: the body does not continue, nothing inside the form sees the
exception, the extents the step had already entered are left, and the
exception is raised by the step.

### Continuations and generators

A continuation cannot be captured across a generator: a `shift` in a
generator whose `reset` is outside it, and a `call/cc` capture that
would cross a generator, raise `KoskariPromptUnreachable`. A `reset`
and its `shift` between two yields of one generator are unaffected.

## See also

- [`dyn-wind`](../reference/specials/compile-dyn-wind.md),
  [`dyn-with`](../reference/specials/dyn-with.md) and
  [`parameterize`](../reference/specials/compile-parameterize.md): the
  forms
- [`call/cc`](../reference/specials/compile-call-cc.md),
  [`reset`](../reference/specials/compile-reset.md) and
  [`shift`](../reference/specials/compile-shift.md): the transfers
- [I/O and effects](io-and-effects.md): `with` and `try`
- [Demand, async, and defer](demand-async-and-defer.md): calls between
  Python and Koskari
