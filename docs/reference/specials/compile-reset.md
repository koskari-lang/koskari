# compile/reset
**Symbol:** `compile/reset`

**Syntax:** `(reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Emits `RESET 0`, then the body inline, ending in a return.
`RESET` pushes a prompt and runs the body in a frame of its own above it,
over the names of the function the `reset` is written in; the body's
return ends the prompt, and its value is the value of the `reset`. The
delimiter carries the default tag, the one a plain `shift` seeks.
[`reset-at`](compile-reset-at.md) establishes a delimiter with a tag of
the caller's choosing.

The body runs in a frame of its own and shares the enclosing function's
names, so:

- A `return` in the body returns from the body: its value is the value of
  the `reset`, and the function the `reset` is written in carries on after
  it. In a copy of the region resumed by a call of a `shift`'s `K`, the same
  `return` gives that copy's value to the `(k ...)` call.
- A `break` or `continue` in the body reaches only a loop written inside
  the body. Aimed at a loop around the `reset`, it raises
  `break outside loop` or `continue outside loop`.
- A `yield` or `yield-from` written in the body is refused at compile time
  with `SpecialError`: the body runs in a frame of its own, not in the
  generator around it. A generator or a function defined inside the
  body is its own function and may yield.
- A `var` in the body is a name of the enclosing function: it is visible
  after the `reset`, and it rebinds a name the function already has. A
  `set!` of a local of the enclosing function writes that local. Every
  copy of the region a `shift`'s `K` resumes shares these names; see
  [Names the handler binds](compile-shift.md#names-the-handler-binds) for
  what that means for a continuation called more than once, and how a
  `let` keeps a binding private.

The body's frame appears in a traceback under the name of the enclosing
function, on the line of the body it was running.

A `shift` that cannot reach a prompt raises `KoskariPromptNotFound` (no
delimiter established) or `KoskariPromptUnreachable` (a delimiter exists but a
crossed boundary denied the capture); see the **Errors** section of
[`compile/shift`](compile-shift.md).

**Examples:**

```koskari
;; (reset (+ 1 (shift k (k 5)))) => 6
(reset
  (+
    1
    (shift k (k 5))))

;; return in the body gives the reset its value, and the
;; function goes on
def f []:
    var log #[]
    var r (reset (log.append "in") (return 7) (log.append "unreached"))
    (log.append r)
    log
;; (f) => ["in", 7]
```
