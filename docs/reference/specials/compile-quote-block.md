# compile/quote-block
**Symbol:** `compile/quote-block`

**Syntax:** `'HEAD: BODY` (reader shorthand) or `(quote-block BLOCK-REPR)`

**Use-case:** Produce a `:block:` cons as literal data -- the block-syntax
analogue of `compile/quote`. The entire indented block (including any nested
sub-blocks) is captured as a `:block:` cons without invoking layout handlers on
the inner structure. Useful for passing block structure as data to macros or
other compile-time code.

**Reader shorthand:** A `'` immediately preceding a block head (e.g. `'begin: ...`)
reads the block as raw **`:block:`** conses and wraps them in a synthetic
**`(:block: (quote) INNER)`** form. **`layout/quote`** lowers that to
**`(quote INNER)`** at layout expansion. Each body line is one entry of the
cons, the list of the line's elements, and sub-blocks remain **`:block:`**
entries.

**Behaviour:** Purely transformative. Rewrites to `(quote BLOCK-REPR)`
and returns that cons for normal compiler dispatch. The `:block:` cons is
treated as opaque data -- no layout expansion takes place on the quoted block.

**Examples:**

```koskari
;; Reader shorthand: produces (:block: (begin) (foo) (bar)) as data
'begin:
  foo
  bar

;; Explicit form
(quote-block (:block: (begin) (foo) (bar)))
```

**Implementation:** `koskari/compile/specials/quasiquote.py`
