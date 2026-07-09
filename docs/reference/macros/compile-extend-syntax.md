# compile/extend-syntax
**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same six transformer heads as
`define-syntax`.

**Behaviour:** Replaces **`NAME`** via **`setq`** with a new **`syntax-rules`** /
**`syntax-match`** / **`macro-rules`** / **`special-rules`** / **`special-match`**
spec (each expansion passes **`NAME`** as the first argument to the builder form).

**Precedence:** The newer rules take precedence over the existing ones (the
combination is `NAME + new` via `Syntax.__add__`). So a base transformer can
carry a generic fall-through case, and a later `extend-syntax` that adds a more
specific case wins wherever both would match. This is the basis of open
dispatch: a base `(config (tp name) …)` fall-through plus per-type
`extend-syntax config` cases keyed on a literal head, as in
`(config (kafka name) …)` (the same shape as `(help (pragma key))`).

**Across modules:** `extend-syntax` composes across imports. After
`import-from mod foo`, extending `foo` in the importing module adds cases that
route there while the base rules still apply to inputs the new cases do not
match. This is what lets a library ship a base macro and have each consumer or
sibling module specialize it.

**Layout:** default **`layout/block`** flat splice at read time.

