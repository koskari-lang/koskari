# layout/switch

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/switch`

**Use-case:** Multi-way equality dispatch in block syntax. Lowers to `switch`
clause lists consumed by `compile/switch`.

**Behaviour:** Does not accept peers. Header must be exactly
`switch VALUE:` (one value expression). Inner `case` and `else` blocks are
left as default block layouts (no dedicated `layout/case` or `layout/else`).
So children are read first by `layout/block` as:

- `case COND:` -> `(case COND (compile/begin BODY...))`
- `else:` -> `(else (compile/begin BODY...))`

`layout/switch` validates and rewrites those child forms into:

- case clause: `(COND-LIST (compile/begin BODY...))`
- else clause: `(else: (compile/begin BODY...))`

It then returns:

`(switch VALUE (COND-LIST (compile/begin ...)) ... (else: (compile/begin ...)))`

Rules:

- body entries must be `case` or `else` blocks only
- `case` requires exactly one condition argument and it is passed through
  as the case's condition list expression
- `else` takes no arguments
- only one `else` is allowed
- `case` cannot appear after `else`
- empty body is allowed and lowers to `(switch VALUE)`

**Syntax:**

```
switch VALUE:
  case COND-LIST:
    BODY
  case COND-LIST2:
    BODY
  else:
    BODY
```

**Examples:**

```scheme
switch x:
  case [1]:
    "one"
  case [2 3]:
    "two-or-three"
  else:
    "meh"
;; -> (switch x
;;      ([1] (compile/begin "one"))
;;      ([2 3] (compile/begin "two-or-three"))
;;      (else: (compile/begin "meh")))

switch x:
  case [1]:
    "one"
;; -> (switch x ([1] (compile/begin "one")))
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_switch`

**Tests:** `tests/reader/layouts/test_switch_layout.py`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
