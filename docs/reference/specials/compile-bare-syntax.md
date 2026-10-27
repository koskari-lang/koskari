# compile/bare-syntax
**Symbol:** `compile/bare-syntax`

**Syntax:** `(bare-syntax NAME SLOT TEMPLATE)`

**Use-case:** A bare arm for a transformer -- the expansion used when the
syntax name appears as a lone atom instead of at the head of a call form.
`(counter 4)` reaches the call arms; a bare `counter` reaches the bare arm.

**Behaviour:** SLOT names the called-by capture, and is the only binding
TEMPLATE may refer to; it holds the symbol as written at the use site. The form
lowers to a `Syntax` carrying only that arm. Bind it with
[`syntax`](../macros/compile-syntax.md) to make a syntax that has
no call arms, or attach it to an existing syntax with
[`extend-syntax`](../macros/compile-extend-syntax.md).

**Block syntax:** `bare-syntax SLOT:` with the template as the body, or
`bare-syntax NAME SLOT:` with a leading name. A multi-form body is wrapped in
`begin`. See [layout/bare-syntax](../layouts/layout-bare-syntax.md).

**Examples:**

```koskari
>>> syntax greeting:
...   bare-syntax slot:
...     "hello"
>>> greeting
"hello"
```

A bare arm added to an existing transformer:

```koskari
>>> syntax counter:
...   syntax-rules []:
...     case (counter n):
...       (+ n 1)
>>> extend-syntax counter:
...   bare-syntax slot:
...     0
>>> counter
0
>>> (counter 4)
5
```

The slot carries the name as written:

```koskari
>>> syntax whoami:
...   bare-syntax me:
...     (str (quote me))
>>> whoami
"whoami"
```

**See also:** [`compile/syntax-rules`](compile-syntax-rules.md),
[`extend-syntax`](../macros/compile-extend-syntax.md),
[Macros and hygiene](../../guide/macros-and-hygiene.md).

**Also:** layout handler [`bare-syntax`](../layouts/layout-bare-syntax.md).
