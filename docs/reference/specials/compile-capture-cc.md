# compile/capture/cc
**Symbol:** `compile/capture/cc`

**Syntax:** `(capture/cc K BODY ...)`

**Use-case:** Lowest-level undelimited continuation capture. Like
`compile/let/cc` but binds `K` in the **current** (ambient) namespace rather
than a fresh child namespace, so `K` remains accessible after the form's CC
block closes. Useful when the continuation needs to outlive the block or be
visible in a scope that already contains `K`.

**Behaviour:** Emits `PUSH_CC`, then `CAPTURE_CC` (captures and pushes the
continuation), `VAR_STORE K` (stores into the current namespace's slot for
`K`), the body as a `begin`, and finally `POP_CC`.

**Examples:**

```koskari
;; Capture into ambient namespace; k accessible later
(capture/cc k
  (begin
    (do-work k)
    (k 0)))
```

**Errors:** As for [`call/cc`](compile-call-cc.md): a capture from inside a
running `__enter__`, `__exit__`, before thunk or after thunk, or one that
would cross a generator, raises `KoskariPromptUnreachable` naming the
boundary.
