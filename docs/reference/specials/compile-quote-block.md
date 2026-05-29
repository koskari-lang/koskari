# compile/quote-block

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/quote-block`

**Syntax:** `'HEAD: BODY` (reader shorthand) or `(compile/quote-block BLOCK-REPR)`

**Use-case:** Produce a `:block:` cons as literal data — the block-syntax
analogue of `compile/quote`. The entire indented block (including any nested
sub-blocks) is captured as a `:block:` cons without invoking any layout
handler. Useful for passing block structure as data to macros or other
compile-time code.

**Reader shorthand:** A `'` immediately preceding a block head (e.g. `'begin: ...`) causes the reader to read the entire block as a raw `:block:` cons and
wrap it in `(compile/quote-block ...)`. Sub-blocks in the body are recursively
read as `:block:` nodes without layout expansion.

**Behaviour:** Purely transformative. Rewrites to `(compile/quote BLOCK-REPR)`
and returns that cons for normal compiler dispatch. The `:block:` cons is
treated as opaque data — no layout expansion takes place.

**Examples:**

```scheme
;; Reader shorthand: produces (:block: (begin) (foo bar)) as data
'begin:
  foo
  bar

;; Explicit form
(compile/quote-block (:block: (begin) (foo bar)))
```

**Implementation:** `koskari/compile/specials/quasiquote.py`

**Tests:** `tests/compile/specials/test_quasiquote.py`, `tests/reader/layouts/test_quasiquote.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
