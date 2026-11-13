# compile/shift
**Symbol:** `compile/shift`

**Syntax:** `(shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt, abort to that prompt, and run the handler `BODY`
there with `K` bound to the continuation. The handler's value is the value
of the `reset`. `K` is a `DelimitedContinuation`; it is called from Koskari
code and cannot be called from Python.

A handler that never calls or keeps `K` wants [`abort`](compile-abort.md),
which leaves the region the same way and captures nothing.

**Behaviour:** Emits `SHIFT_VAR 0` naming `K`, then the handler inline:
`BODY` and a return. The instruction binds `K` to the continuation before
it aborts. The handler runs in a frame of its own over the names of the
scope the `shift` is written in. `K` is a
name of that scope, bound as [`for-each`](compile-for-each.md) binds its
variable, and a `var` in `BODY` is a name of that scope too; see
[Names the handler binds](#names-the-handler-binds). Calling `K` packs its arguments the way
`return` does: `(k)` resumes with `None`, `(k v)` with `v`, `(k a b)` with the
tuple `(a, b)`, and a call with keywords, `(k a x: 1)`, with a `values`
instance over all of them. The resumed slice is the body of `K`: a `raise`
in it propagates from the `(k ...)` call site, and a `break` or `continue`
in it reaches only a loop opened inside the slice. The slice ends where the
`reset`'s body ends, and the body runs in a frame of its own (see
[`reset`](compile-reset.md)): a `return` in the body's part of the slice
gives that call's value to the `(k ...)` call. A function the slice calls
returns as usual. A `yield` written in the body is refused at compile
time, so the slice never yields to a generator outside it.

The handler runs after the shift has aborted to its `reset`: the captured
region is left first, and the handler runs with the `reset`'s prompt
still in place, so a `shift` in `BODY` captures up to the same `reset`. The
handler reads the names of the scope the `shift` is written in, including
the locals of a function the region called. A `dyn-wind`
around the `shift` runs its after thunk and a `dyn-with` exits before the
handler runs; when that after thunk or exit raises, the handler does
not run and the region carries on where the raise is caught. A
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
refused at compile time with `SpecialError`: the handler runs in a frame
of its own, not in the generator the `reset` is in. A generator defined
inside `BODY` is its own function and may yield. The handler's frame
appears in a traceback under the name of the function the `shift` is
written in.

## Names the handler binds

`K` and any `var` in `BODY` belong to the scope the `shift` is written in.
Three other forms bind a name the same way: the variable of a
[`for-each`](compile-for-each.md), the name a [`with`](with.md) binds its
manager's value to, and the name an `except` clause of a
[`try`](compile-try.md) binds the exception to. In each case:

- the name is readable after the form, holding the last value bound;
- the form rebinds a name the scope already has;
- every entry of the form in one activation of the function writes the
  same name.

The third point matters when a continuation is called more than once.
A copy of the region shares the function's names with the region it was
copied from, so a form the copy reaches again rebinds them under a handler
that is still running:

```koskari
def shared []:
    var seen #[]
    reset:
        for-each [x #[1 2]]:
            shift k:
                (k None)
                (seen.append x)
    seen
;; (shared) => [2, 2]
```

The first handler's `(k None)` runs the rest of the loop, which binds `x`
to 2 and reaches the `shift` again, binding `k` to a new continuation.
When control comes back to the first handler, `x` is 2 and `k` is the
second continuation.

A [`let`](compile-let.md) gives a binding that belongs to one entry.
Inside the handler, it keeps what that entry needs:

```koskari
def kept []:
    var seen #[]
    reset:
        for-each [x #[1 2]]:
            shift k:
                let [[again k] [mine x]]:
                    (again None)
                    (seen.append mine)
    seen
;; (kept) => [2, 1]
```

Around a form, an empty `let` keeps the form's name out of the enclosing
scope altogether, and gives each entry of the form a name of its own:

```koskari
def private []:
    var k "outer"
    reset:
        let []:
            shift k:
                1
    k
;; (private) => "outer"
```

The same `let []` around a `for-each`, a `with` or a `try` keeps their
names private in the same way. A `shift` written in a function of its own,
as a library's helper usually is, needs neither: each call of the function
has its own names.

**Errors:** Capture can fail in two structurally different ways, each with its
own exception under the `KoskariContinuationError` category (see
[Error codes](../error-codes.md)):

- `KoskariPromptNotFound` -- no matching prompt anywhere on the dynamic
  chain: `shift has no enclosing reset`. Establish a delimiter with
  [`reset`](compile-reset.md).
- `KoskariPromptUnreachable` -- a matching prompt exists, but a node between
  the shift and the prompt denied the capture: `cannot abort across a
  generator boundary`. A `shift` made inside a running `__enter__`,
  `__exit__`, before thunk or after thunk of a `with`, `dyn-with` or
  `dyn-wind` in the region is refused the same way. Adding a `reset` will
  not help; restructure so the capture does not cross the boundary. The
  denying boundary is recorded on the exception's `boundary` attribute
  (`generator`, `enter`, `exit`, `before` or `after`). It is also a
  `RuntimeError`.

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
