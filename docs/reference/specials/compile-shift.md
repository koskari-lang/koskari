# compile/shift
**Symbol:** `compile/shift`

**Syntax:** `(shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt and bind it to `K`, then run `BODY` in place of the
captured region. `K` is a `DelimitedContinuation`; it is called from Koskari
code and cannot be called from Python.

**Behaviour:** Emits `PUSH_SHIFT 0`, declares `K` as a local variable, emits
`VAR_STORE K` (binds the captured delimited continuation), compiles body with
`helper_begin`, emits `POP_SHIFT 0`. `K` is a binding in the enclosing scope,
as a loop variable or a `with` name is: it stays bound after the form, it
rebinds an existing `K`, and a later `shift` in the slice that reuses the
name rebinds it again. A handler that invokes its continuation more than
once therefore takes a name no later `shift` in the slice uses, since each
shot runs the slice against the same scope. Calling `K` packs its arguments the way
`return` does: `(k)` resumes with `None`, `(k v)` with `v`, `(k a b)` with the
tuple `(a, b)`, and a call with keywords, `(k a x: 1)`, with a `values`
instance over all of them. The resumed slice is the body of `K`: a `raise`
in it propagates from the `(k ...)` call site, and a `break` or `continue`
in it reaches only a loop opened inside the slice. The slice ends where the
`reset` ends, so the function the `reset` is written in has no boundary
inside it: a `return` in that function's part of the slice raises
`KoskariPromptUnreachable`, with `operation` `return` and `boundary`
`continuation`. A function the slice calls returns as usual. A `yield` in
that part of the slice raises the same error with `operation` `yield`: the
slice has no generator of its own, wherever `K` is called from.

`BODY` runs after the shift has aborted to its `reset`: the captured region
is left first, and `BODY` stands where the region stood. A `dyn-wind`
around the `shift` runs its after thunk and a `with` exits before `BODY`
does. A `finally` body inside the region is local to its `try` and does
not run on the abort. A `try` inside the region does not catch what `BODY`
raises, and a `parameterize` inside the region is not in effect for it.
Each call of `K` enters a copy of the region, so the `dyn-wind` extents are
entered again per call, and a copy of a `try` runs its `finally` body when
that copy exits locally:
`(reset (dyn-wind before (shift k (k 5)) after))` runs `before` and `after`
twice, once around the abort and once around the call.

`BODY` has only the loops written inside it. A `break` or `continue` there
with no loop of its own raises `break outside loop` or `continue outside
loop`, whatever loop surrounds the `shift` or the `reset`. A `yield` in
`BODY` suspends the generator the `reset` is in. A `return` in `BODY`
returns from the function when the `reset` is in that same function. When
the `shift` is in a function called from the region, that function's caller
was left with the region, and the `return` raises
`KoskariPromptUnreachable` with `operation` `return` and `boundary` `shift`.

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

;; Two shifts in one slice, each shot of k reaching j: distinct names,
;; since both bind in the same scope
(reset
  (#tuple (shift k (#tuple (k 1) (k 2)))
          (shift j (#tuple (j 10) (j 20)))))
;; => (((1, 10), (1, 20)), ((2, 10), (2, 20)))
```
