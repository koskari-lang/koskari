# compile/extend-syntax
**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same six transformer heads as
`define-syntax`.

**Behaviour:** Replaces **`NAME`** via **`setq`** with a new **`syntax-rules`** /
**`syntax-match`** / **`macro-rules`** / **`special-rules`** / **`special-match`**
spec (each expansion passes **`NAME`** as the first argument to the builder form).

**Layout:** default **`layout/block`** flat splice at read time.

