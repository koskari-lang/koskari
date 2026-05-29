# compile/locals

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/locals`

**Syntax:** `(compile/locals)`

**Use-case:** Obtain the current frame's namespace (locals), analogous to
Python's `locals()`.

**Behaviour:** Emits `LOCALS` (argument 0). Pushes the current frame's namespace
onto the stack. Any arguments are rejected with `SpecialError`.

**Examples:**

```scheme
(compile/locals)
```

**Implementation:** `koskari/compile/specials/variables.special_locals`

**Tests:** `tests/compile/specials/test_namespace_specials.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
