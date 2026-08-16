# compile/extend-syntax
**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same transformer heads as
`define-syntax`, including `bare-syntax`.

**Behaviour:** Rebinds **`compile/NAME`** via **`setq`** with the combination of
the existing transformer and a new **`syntax-rules`** / **`syntax-match`** /
**`macro-rules`** / **`macro-match`** / **`special-rules`** / **`special-match`** /
**`bare-syntax`** spec (each expansion passes the written **`NAME`** as the
builder's identity argument).

**Documentation:** the original docstring carries forward; a docstring given
in the extension is appended to it rather than replacing it.

**Bare forms:** a transformer carries at most one bare form, so extending
with `bare-syntax` replaces any existing bare arm rather than stacking.

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

