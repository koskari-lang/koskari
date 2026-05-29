# compile/capture/cc

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/capture/cc`

**Syntax:** `(compile/capture/cc K BODY ...)`

**Use-case:** Lowest-level undelimited continuation capture. Like
`compile/let/cc` but binds `K` in the **current** (ambient) namespace rather
than a fresh child namespace, so `K` remains accessible after the form's CC
block closes. Useful when the continuation needs to outlive the block or be
visible in a scope that already contains `K`.

**Behaviour:** Emits `PUSH_CC`, then `CAPTURE_CC` (captures and pushes the
continuation), `VAR_STORE K` (stores into the current namespace's slot for
`K`), the body via `helper_begin`, and finally `POP_CC`.

**Examples:**

```scheme
;; Capture into ambient namespace; k accessible later
(compile/capture/cc k
  (compile/begin
    (compile/call do-work k)
    (compile/call k 0)))
```

**Tests:** `tests/compile/specials/test_call_cc.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
