# labels and flet

**Status:** Implemented (archived). Shipped as bootstrap macros
**`compile/flet`** (surface **`flet`**) and **`compile/labels`** (surface
**`labels`**) in **`koskari/bootstrap/_functions.ki`**, following the same
pattern as the original Sibilant implementations.

**Canonical summary:** [MACROS.md](../MACROS.md) § compile/flet and
§ compile/labels.

**User documentation:**

- [docs/reference/macros/compile-flet.md](../../docs/reference/macros/compile-flet.md)
- [docs/reference/macros/compile-labels.md](../../docs/reference/macros/compile-labels.md)

**Corpus:** `docs/corpus/records/functions.yaml` (`macro-flet-*`,
`macro-labels-*`) and `docs/corpus/records/functions-invalid.yaml`
(`macro-flet-sibling-reference-invalid-001`).

**Tests:** `tests/importer/test_flet.py`, `tests/importer/test_labels.py`.

---

## As-built

Both are plain **`syntax-rules`** macros that expand to existing specials — no
compiler or VM changes — matching the Sibilant spellings.

### flet

```koskari
(flet [[name formals fbody ...] ...] body ...)
  ;; →
(let [[name (lambda formals fbody ...)] ...] body ...)
```

Each declaration becomes a **`let`** binding whose value is an anonymous
**`lambda`**. Because **`let`** initializers evaluate in the enclosing scope, a
binding shadows an outer function of the same name but does not see its siblings
in the same **`flet`** (no mutual recursion).

### labels

```koskari
(labels [[name formals fbody ...] ...] body ...)
  ;; →
(let []
  (define name (function name formals fbody ...))
  ...
  body ...)
```

Each declaration is **`define`**d inside a shared (empty) **`let`** scope, so the
names are visible to one another and the functions can be mutually recursive.
This is the empty-`let`-plus-`define` pattern used in Sibilant.

As with any local binding, a **`labels`** binding whose name is also a **macro**
does not take effect in call position — macros expand before names resolve, so
they always win. Names bound to runtime functions are shadowed normally.

## Nested-ellipsis dependency

Both macros capture each declaration's body under two ellipsis levels
(**`fbody ...`** nested inside the declaration-list **`...`**). This required
depth-aware nested-ellipsis expansion in **`syntax-rules`** templates, which the
prior single-level `expand_template_with_hygiene` did not support (it mis-spliced
the inner column as a raw form). The expander now peels exactly one ellipsis
dimension per template **`...`** via progressive per-level binding slicing; see
`koskari/hygiene.py` and `tests/hygiene/test_hygiene.py`
(`test_nested_ellipsis_*`).

## Related

- [../MACROS.md](../MACROS.md) — bootstrap macro catalog.
- **`koskari/bootstrap/_functions.ki`** — **`function`**, **`define`**,
  **`function-compose`**, **`decorate`**.
- [../SPECIALS.md](../SPECIALS.md) — **`compile/let`**, **`compile/function`**.
