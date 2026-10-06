# Block quasiquote and `#block:` layout expansion

A block backtick reads raw `#block:` conses and wraps them in a synthetic
`(#block: (quasiquote) INNER)` form. The `layout/quasiquote` handler lowers that
to `(quasiquote INNER)` for compilation. The quasiquote fills the unquote sites
and leaves the `#block:` structure alone, so a filled block is still reader
form, not source the compiler accepts.

Module reading expands blocks as it goes. Macro expansion applies the layout
handlers to a transformer's output before the captured material is unboxed,
so a macro whose template is a
[block quasiquote](../specials/compile-quasiquote-block.md) needs no extra
step. Code that builds a `#block:` cons at run time and wants source
out of it calls `apply-layout` or `apply-layouts`.

## `apply-layout`

`(apply-layout BLOCK TARGET)` expands one form. TARGET is a `LayoutReader`, or
a namespace whose module owns one -- `(globals)` at module level. A `#block:`
cons is expanded through the `layout/HEAD` handler for its head; any other form
is returned unchanged. Expansion is depth-first, and peer blocks that a handler
declares are consumed from the block's entries.

```koskari
>>> define forms '(1 2 3)
>>> define tmpl:
...   `begin:
...     ,@forms
>>> tmpl
(#block: (begin) (#splice: 1 2 3))
>>> (apply-layout tmpl (globals))
(begin 1 2 3)
>>> (evaluate (apply-layout tmpl (globals)) (globals))
3
```

## `apply-layouts`

`(apply-layouts FORMS TARGET)` walks a proper cons list of forms and expands
each one, returning the list of results. Forms that are not blocks pass
through. A declared peer block is consumed from the spine following its primary
block, so `if` and its `else` peer collapse into one form:

```koskari
>>> define b1:
...   `if (> 2 1):
...     "yes"
>>> define b2:
...   `begin:
...     (+ 1 2)
>>> (apply-layouts (cons b1 b2 '(x) nil) (globals))
((cond ((> 2 1) "yes")) (begin (+ 1 2)) (x))
```

`if` lowers to [`cond`](../specials/compile-cond.md), which is the special the
layout reads to.

**See also:** [Block quote](../specials/compile-quote-block.md),
[Block quasiquote](../specials/compile-quasiquote-block.md),
[Layout forms](../layouts/index.md).
