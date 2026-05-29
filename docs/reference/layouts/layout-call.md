# layout/call

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/call`

**Use-case:** Block syntax for a function call: the callee is the only header
argument; each indented body form becomes an argument to **`compile/call`**.
Useful when a call has several arguments and you want them on separate lines
without nesting parentheses for the whole call.

**Behaviour:** Does not accept peers. The header must be exactly
`(layout/call FN)` — no extra forms after **`FN`** (raises `LayoutError` if
there are header arguments). Produces **`(compile/call FN ARG ...)`**. The body
is **not** wrapped in **`compile/begin`**.

Before building the call, the handler walks the body list: if a body form is a
**pair whose car is a keyword**, it is rewritten in place into **two** adjacent
arguments (the keyword, then the cdr), matching the usual **`compile/call`**
keyword-argument layout. Ordinary forms are unchanged.

For **`*:`**, splices, and the full **`compile/call`** surface, see
[SPECIALS.md](../../design/SPECIALS.md) § compile/call.

**Syntax:**

```
layout/call FN:
  ARG-FORM
  ...
```

A line such as **`NAME: VALUE`** is read as one pair form; the layout normalizes
it to keyword plus value as described above.

**Examples:**

```scheme
layout/call print:
  "hello"
;; → (compile/call print "hello")

layout/call foo:
  bar
  (baz)
;; → (compile/call foo bar (baz))

layout/call foo:
  bar
  (baz)
  alpha: beta
;; → (compile/call foo bar (baz) alpha: beta)
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_call`

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
