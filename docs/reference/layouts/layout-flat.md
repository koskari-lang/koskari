# layout/flat

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/flat`

**Use-case:** Block syntax where the read result should be a single list: the
header forms followed by every body form, with no extra structure between them.
Used as the implementation for **`layout/begin`** (same handler object) and as
the target of **`define layout/… layout/flat`** aliases in **`basics.ki`**
(**`layout/define-syntax`**, **`layout/extend-syntax`**).

**Behaviour:** Does not accept peers. **`source`** is **`(header . body)`** as
for other layouts; the handler extends the header list with the body list and
returns it. Example: header **`(foo x)`** and body **`(bar baz)`** yields
**`(foo x bar baz)`**.

**Syntax:**

```
HEAD ARG ... :
  BODY-FORM
  ...
```

**Examples:**

```scheme
foo x:
  bar
  baz
;; → (foo x bar baz)
```

**Implementation:** `koskari/reader/layouts/basics.layout_flat`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
