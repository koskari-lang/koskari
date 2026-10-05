# compile/shift
**Symbol:** `compile/shift`

**Syntax:** `(shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt and bind it to `K`, then run `BODY`. `K` is a
`DelimitedContinuation` that cannot be called from Python (only from within a
Koskari eval loop via `INVOKE_K` or `INVOKE_K_VAR`).

**Behaviour:** Emits `PUSH_SHIFT 0`, declares `K` as a local variable, emits
`VAR_STORE K` (binds the captured delimited continuation), compiles body with
`helper_begin`, emits `POP_SHIFT 0`. Calling `K` packs its arguments the way
`return` does: `(k)` resumes with `None`, `(k v)` with `v`, `(k a b)` with the
tuple `(a, b)`, and a call with keywords, `(k a x: 1)`, with a `values`
instance over all of them.

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
  exception's `boundary` attribute.

Both carry `prompt_id` (the delimiter id the capture was seeking) and
`operation` (the surface construct, `shift`). A library that establishes its
own delimiter can use `prompt_id` to recognize failures against it and
translate them into its own vocabulary, as `koskari.solve` does with
`NoActiveSearch`.

**Examples:**

```koskari
;; Capture k; invoke immediately
(reset
  (shift k
    (k "value")))
```
