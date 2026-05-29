# compile/globals

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/globals`

**Syntax:** `(compile/globals)`

**Use-case:** Obtain the module globals namespace for the current frame:
walk `KosNamespace_PARENT` from the frame's namespace to the outermost parent
(analogous to Python's `globals()`). Useful for reflection, explicit binding
into the module namespace, or passing the globals dict to APIs that expect it.

**Behaviour:** Emits `GLOBALS` (argument 0). Pushes that namespace onto the
stack as the expression's value. Any arguments are rejected with `SpecialError`.

**Examples:**

```scheme
;; Module globals namespace for the current frame
(compile/globals)
```

**Implementation:** `koskari/compile/specials/variables.special_globals`

**Tests:** `tests/compile/specials/test_namespace_specials.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
