# compile/reset-at
**Symbol:** `compile/reset-at`

**Syntax:** `(reset-at TAG BODY ...)`

**Use-case:** Establish a prompt delimiter that carries a tag, for a
[`shift-at`](compile-shift-at.md) seeking that same tag. A library uses a
tag of its own so that its delimiter is not the one a caller's plain
[`reset`](compile-reset.md) and `shift` find, and the caller's are not the
ones the library finds.

**Behaviour:** Evaluates `TAG`, compiles the body as a function of no
arguments, and emits `RESET 1` carrying it (the tag is taken from the
stack). The form's value is the body's value, or the value of the handler
of a `shift-at` that captured up to it. The body is a function of its own
as it is for [`reset`](compile-reset.md): what that page says of
`return`, `break`, `continue`, `yield` and `var` in the body holds here
unchanged.

A tag is any object, and tags match by identity: two equal lists are two
tags. `None` is the default tag, the one `reset` and `shift` use, so
`(reset-at None BODY ...)` is `(reset BODY ...)`. A fresh tag no other
code can name comes from `gensym`. A keyword or a symbol is the same
object wherever it is written, which makes it a tag any code can name.

Delimiters nest. A `shift-at` stops at the nearest `reset-at` carrying its
tag and crosses delimiters of other tags on the way; see
[`shift-at`](compile-shift-at.md).

**Examples:**

```koskari
;; a private tag
(var search (gensym "search"))

;; (reset-at search (+ 1 (shift-at search k (k 5)))) => 6
(reset-at search
  (+ 1 (shift-at search k (k 5))))

;; a plain shift does not see a tagged delimiter: it stops at the
;; plain reset, and 7 replaces that reset only => 107
(reset-at search
  (+ 100 (reset (+ 10 (shift k 7)))))
```

As a layout block the tag follows the head:

```koskari
reset-at search:
    + 1:
        shift-at search k:
            k 5
```
