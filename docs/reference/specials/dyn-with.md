# dyn-with

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/dyn-with`

**Syntax:** `(dyn-with [name? expr] body ...)`

Same syntax as `with`; optional `name` binds the enter result.

**Use-case:** Multi-shot context manager that participates in continuation
transfer plans (shift/reset, undelimited call/cc). On non-local transfer,
enter/exit run as wind/unwind steps; re-entry creates a fresh extent
instance.

**Behaviour:** Same as `with` except emits PUSH_DYN_WITH and POP_DYN_WITH.
Expression value is always `None`. See
[interpreter/with-dyn-with.md](../../design/interpreter/with-dyn-with.md).

**Examples:**

```scheme
;; Capture continuation inside dyn-with; invoke outside runs exit then enter
(dyn-with [r (make-resource)]
  (compile/call k (compile/refq r)))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
