# layout/begin
**Symbol:** `layout/begin`

**Use-case:** A block whose head is **`begin`** (or **`layout/begin`** when
spelled that way): read as a flat list **`(begin FORM …)`** so the body forms
sit directly after **`begin`**, not inside **`compile/begin`**. Useful for an
indented sequence of forms at the same level as a surrounding expression.

**Behaviour:** Same handler as **`layout/flat`** (registered under the
**`layout/begin`** name). Does not accept peers. Produces **`(begin BODY-FORM
…)`**.

**Syntax:**

```
begin:
  FORM
  ...
```

**Examples:**

```koskari
begin:
  (bar)
  (baz)

;; → (begin (bar) (baz))
```

**Implementation:** `koskari/reader/layouts/basics` (`layout_begin` is the same
callable as **`layout_flat`**)

