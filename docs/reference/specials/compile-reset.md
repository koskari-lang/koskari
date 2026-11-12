# compile/reset
**Symbol:** `compile/reset`

**Syntax:** `(reset BODY ...)`

**Use-case:** Establish a prompt delimiter for delimited continuations
(`compile/shift`). The body is evaluated with a prompt on the dynamic chain;
`compile/shift` inside the body can capture the continuation up to (but not
including) this prompt.

**Behaviour:** Compiles the body as a function of no arguments, closed over
the scope the `reset` is written in, and emits `RESET 0` carrying it.
`RESET` pushes a prompt and calls the body above it; the body's return
ends the prompt, and its value is the value of the `reset`. The delimiter
carries the default tag, the one a plain `shift` seeks.
[`reset-at`](compile-reset-at.md) establishes a delimiter with a tag of
the caller's choosing.

The body is a function of its own, so:

- A `return` in the body returns from the body: its value is the value of
  the `reset`, and the function the `reset` is written in carries on after
  it. In a copy of the region resumed by a call of a `shift`'s `K`, the same
  `return` gives that copy's value to the `(k ...)` call.
- A `break` or `continue` in the body reaches only a loop written inside
  the body. Aimed at a loop around the `reset`, it raises
  `break outside loop` or `continue outside loop`.
- A `yield` or `yield-from` written in the body is refused at compile time
  with `SpecialError`: the body is a function the `reset` calls, not part
  of a generator around it. A generator or a function defined inside the
  body is its own function and may yield.
- A `var` in the body is local to the body. A `set!` of a local of the
  enclosing function writes that local through the closure.

The body's frame appears in a traceback under the name `<reset>`.

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
