# compile/shift
**Symbol:** `compile/shift`

**Syntax:** `(shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt, abort to that prompt, and call the handler -- a
function of `K` whose body is `BODY` -- with the continuation there. The
handler's value is the value of the `reset`. `K` is a
`DelimitedContinuation`; it is called from Koskari code and cannot be
called from Python.

**Behaviour:** Compiles the handler as a function of the one parameter `K`,
closed over the scope the `shift` is written in, and emits `SHIFT 0`
carrying it. `K` is the handler's parameter only: nothing is bound in the
enclosing scope, a `var` in `BODY` is local to the handler, and two
`shift` forms in one slice may use the same name. Calling `K` packs its arguments the way
`return` does: `(k)` resumes with `None`, `(k v)` with `v`, `(k a b)` with the
tuple `(a, b)`, and a call with keywords, `(k a x: 1)`, with a `values`
instance over all of them. The resumed slice is the body of `K`: a `raise`
in it propagates from the `(k ...)` call site, and a `break` or `continue`
in it reaches only a loop opened inside the slice. The slice ends where the
`reset`'s body ends, and the body is a function of its own (see
[`reset`](compile-reset.md)): a `return` in the body's part of the slice
gives that call's value to the `(k ...)` call. A function the slice calls
returns as usual. A `yield` written in the body is refused at compile
time, so the slice never yields to a generator outside it.

The handler runs after the shift has aborted to its `reset`: the captured
region is left first, and the handler is called with the `reset`'s prompt
still in place, so a `shift` in `BODY` captures up to the same `reset`. The
handler reads the names of the scope the `shift` is written in, including
the locals of a function the region called. A `dyn-wind`
around the `shift` runs its after thunk and a `dyn-with` exits before the
handler is called; when that after thunk or exit raises, the handler is
not called and the region carries on where the raise is caught. A
`finally` body and a plain `with` inside the region are local to
their body and do not run on the abort: the `with`'s manager stays entered.
A `try` inside the region does not catch what `BODY` raises, and a
`parameterize` inside the region is not in effect for it. Each call of `K`
enters a copy of the region, so the `dyn-wind` and `dyn-with` extents are
entered again per call, and a copy of a `try` or a plain `with` runs its
`finally` body or exit when that copy exits locally:
`(reset (dyn-wind before (shift k (k 5)) after))` runs `before` and `after`
twice, once around the abort and once around the call. A call of `K`
enters each `dyn-wind` and `dyn-with` before anything inside it, outermost
first; when a before thunk or enter raises, nothing inside it sees the
exception, and it reaches the `(K ...)` call as the extents already
entered unwind.

`BODY` has only the loops written inside it. A `break` or `continue` there
with no loop of its own raises `break outside loop` or `continue outside
loop`, whatever loop surrounds the `shift` or the `reset`. A `return` in
`BODY` returns from the handler, so its value is the value of the `reset`,
wherever the `shift` is written. A `yield` or `yield-from` in `BODY` is
refused at compile time with `SpecialError`: the handler is a function of
`K`, not part of the generator the `reset` is in. A generator defined
inside `BODY` is its own function and may yield.

**Errors:** Capture can fail in two structurally different ways, each with its
own exception under the `KoskariContinuationError` category (see
[Error codes](../error-codes.md)):

- `KoskariPromptNotFound` -- no matching prompt anywhere on the dynamic
  chain: `shift has no enclosing reset`. Establish a delimiter with
  [`reset`](compile-reset.md).
- `KoskariPromptUnreachable` -- a matching prompt exists, but a node between
  the shift and the prompt denied the capture: `shift cannot capture across a
  generator boundary`. Adding a `reset` will not help; restructure so the
  capture does not cross the boundary. The denying boundary is recorded on the
  exception's `boundary` attribute. It is also a `RuntimeError`.

Both carry `tag` (the prompt tag the capture was seeking, compared by
identity; `None` is the default tag `reset` and `shift` use) and
`operation` (the surface construct, `shift`). A library that
establishes its own delimiter can use `tag` to recognize failures against it and
translate them into its own vocabulary, as `koskari.solve` does with
`NoActiveSearch`.

**Examples:**

```koskari
;; Capture k; invoke immediately
(reset
  (shift k
    (k "value")))

;; Two shifts in one slice, each shot of k reaching j
(reset
  (#tuple (shift k (#tuple (k 1) (k 2)))
          (shift j (#tuple (j 10) (j 20)))))
;; => (((1, 10), (1, 20)), ((2, 10), (2, 20)))

;; return in the handler gives the reset its value
def h []:
    var r (reset (+ 1 (shift k (return 5))))
    (* 10 r)
;; (h) => 50
```
