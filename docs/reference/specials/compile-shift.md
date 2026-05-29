# compile/shift

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/shift`

**Syntax:** `(compile/shift K BODY ...)`

**Use-case:** Capture the current delimited continuation up to the nearest
`compile/reset` prompt and bind it to `K`, then run `BODY`. `K` is a
`DelimitedContinuation` that cannot be called from Python (only from within a
Koskari eval loop via `INVOKE_K` or `INVOKE_K_VAR`).

**Behaviour:** Emits `PUSH_SHIFT 0`, declares `K` as a local variable, emits
`VAR_STORE K` (binds the captured delimited continuation), compiles body with
`helper_begin`, emits `POP_SHIFT 0`.

**Examples:**

```scheme
;; Capture k; invoke immediately
(compile/reset
  (compile/shift k
    (compile/call k "value")))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
