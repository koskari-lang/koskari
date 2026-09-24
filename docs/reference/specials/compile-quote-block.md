# Block quote
**Syntax:** `'HEAD: BODY` (reader shorthand); `#'HEAD: BODY` keeps hygiene
marks

There is no `quote-block` special. A `'` immediately before a block head is a
reader shorthand that lowers to [`compile/quote`](compile-quote.md) over the
block's raw `block:` cons, through the `layout/quote` handler; `#'` lowers to
[`compile/quote-syntax`](compile-quote-syntax.md) through
`layout/quote-syntax`.

**Use-case:** Capture an indented block as literal data. The whole block,
including any nested sub-blocks, becomes a `block:` cons without running layout
handlers on it, so macros and other compile-time code receive the block
structure itself.

**Behaviour:** The reader reads the block header and body raw and wraps the
result in a synthetic `(block: (quote) INNER)` form. The `layout/quote` handler
unboxes that to `(compile/quote INNER)`, which the compiler treats as an
ordinary quote. Nothing expands the quoted block.

Inside the cons, the head line is the first entry, each following body line is
the list of that line's elements, and a nested block is a nested `block:` entry.

The `'` must open the statement -- the shorthand reads the block starting at
that column, so it cannot sit in the middle of a line. It reads the primary
block only; a peer block such as `else:` written at the same indentation as the
quoted head is not gathered, and stays a sibling of the enclosing form.

**Examples:**

```koskari
>>> define form:
...   'begin:
...     foo
...     bar
>>> form
(block: (begin) (foo) (bar))
```

A nested block stays a nested `block:` entry:

```koskari
>>> define form:
...   'begin:
...     if flag:
...       yes
...     else:
...       no
>>> form
(block: (begin) (block: (if flag) (yes)) (block: (else) (no)))
```

**See also:** [`compile/quote`](compile-quote.md),
[Block quasiquote](compile-quasiquote-block.md),
[layout forms](../layouts/index.md).
