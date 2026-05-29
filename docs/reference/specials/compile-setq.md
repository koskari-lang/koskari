# compile/setq

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/setq`

**Syntax:** `(compile/setq SYM EXPR)`

**Use-case:** Store the value of EXPR into the variable named SYM (must already
exist in scope or be requested from an enclosing scope). The form evaluates to
None.

**Behaviour:** Requests the variable name, compiles EXPR, emits VAR_STORE, then
pushes CONST None so the form has a value.

**Note:** The `set!` macro expands symbol assignment to this form; multi-value
assignment uses `compile/setq-values`. Other `set!` shapes use `compile/inplace`,
item/attr/ref stores, etc. See [MACROS.md](../../design/MACROS.md).

**Examples:**

```scheme
(compile/setq x 42)
(compile/setq counter (compile/call + counter 1))
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
