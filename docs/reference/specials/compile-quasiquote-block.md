# Block quasiquote
**Syntax:** `` `HEAD: BODY `` (reader shorthand); `` #`HEAD: BODY `` keeps
hygiene marks

There is no `quasiquote-block` special. A backtick immediately before a block
head is a reader shorthand that lowers to
[`compile/quasiquote`](compile-quasiquote.md) over the block's raw `#block:`
cons, through the `layout/quasiquote` handler; `` #` `` lowers to
[`compile/quasiquote-syntax`](compile-quasiquote-syntax.md) through
`layout/quasiquote-syntax`.

**Use-case:** Capture an indented block as a template, with `,EXPR` and
`,@EXPR` unquote sites active throughout the header and the body. This is how
macros are written in block syntax.

**Behaviour:** The reader installs the `,` and `,@` reader macros for the
duration of the header and body read, reads the block raw, and wraps it in a
synthetic `(#block: (quasiquote) INNER)` form. The `layout/quasiquote` handler
unboxes that to `(compile/quasiquote INNER)`. The quasiquote fills the unquote
sites and leaves the `#block:` structure in place; it runs no layout handlers.

After the header, a `#block:` holds one entry per body line: a `#line:` cons
over that line's elements, or a nested block. A line holding only an
unquote-splicing form is marked `#splice:`, so the spliced forms spread into
one `(#line: FORM)` entry each when the filled block is expanded. An
unquote-splicing sharing a line with other elements splices into that line.

The backtick must open the statement, and it reads the primary block only --
a peer written at the same indentation as the quasiquoted head is not gathered.
Peers nested inside the block are ordinary nested `#block:` entries.

**Examples:**

```koskari
>>> define forms '(1 2 3)
>>> define tmpl:
...   `begin:
...     ,@forms
>>> tmpl
(#block: (begin) (#splice: 1 2 3))
```

A nested peer stays a nested entry:

```koskari
>>> define cnd '(> n 1)
>>> define thn "big"
>>> define els "small"
>>> define q:
...   `begin:
...     if ,cnd:
...       ,thn
...     else:
...       ,els
>>> q
(#block: (begin) (#block: (if (> n 1)) (#line: "big")) (#block: (else) (#line: "small")))
```

**Expansion:** A filled block is still reader form. Macro expansion runs the
layout handlers over a transformer's output before the captured material is
unboxed, so a macro whose template is a block quasiquote needs no extra step. A pipeline that builds a
block at run time and wants source out of it calls
[`apply-layout`](../runtime/block-quasiquote-and-block-layout-expansion.md)
itself:

```koskari
>>> (apply-layout tmpl (globals))
(begin 1 2 3)
```

**Nesting:** A nested block quasiquote is ordinary nested quasiquote --
the level increases, and the inner template is reconstructed as data rather
than filled.

**See also:** [`compile/quasiquote`](compile-quasiquote.md),
[Block quote](compile-quote-block.md),
[Block quasiquote and layout expansion](../runtime/block-quasiquote-and-block-layout-expansion.md).
