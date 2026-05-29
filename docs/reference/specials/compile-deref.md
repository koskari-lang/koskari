# compile/deref

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/deref`

**Syntax:** `(compile/deref EXPR)`

**Use-case:** Dereference a mutable reference. Compiles EXPR (which must produce
a Ref on the stack), then emits the DEREF opcode to call `ref.deref()` and push
the result. The `*identifier` reader atom expands to `(compile/deref identifier)`.
The runtime `deref` builtin remains available for higher-order use (e.g.
`(map deref refs)`).

**Behaviour:** Compiles EXPR, emits KOS_OP_DEREF. Fast path; avoids a full call.

**Examples:**

```scheme
(compile/deref r)   ; compiles expr for r, emits DEREF
*r                  ; reader: (compile/deref r)
(deref r)           ; runtime call; use when passing deref as first-class fn
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
