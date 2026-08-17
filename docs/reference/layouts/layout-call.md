# layout/call
**Symbol:** `layout/call`

**Use-case:** Block syntax for a function call: the callee is the only header
argument; each indented body form becomes an argument to **`compile/call`**.
Useful when a call has several arguments and you want them on separate lines
without nesting parentheses for the whole call.

**Behaviour:** Does not accept peers. The header must be exactly
`(call FN)` — no extra forms after **`FN`** (raises `LayoutError` if
there are header arguments). Produces **`(call FN ARG ...)`**. The body
is **not** wrapped in **`compile/begin`**.

Before building the call, the handler walks the body list: if a body form is a
**pair whose car is a keyword**, it is rewritten in place into **two** adjacent
arguments (the keyword, then the cdr), matching the usual **`compile/call`**
keyword-argument layout. Ordinary forms are unchanged.

For **`*:`**, splices, and the full **`compile/call`** surface, see
[compile/call](../specials/compile-call.md).

**Syntax:**

```
call FN:
  ARG-FORM
  ...
```

A line such as **`NAME: VALUE`** is read as one pair form; the layout normalizes
it to keyword plus value as described above.

**Examples:**

```koskari
call print:
  "hello"
;; → (call print "hello")

call foo:
  bar
  (baz)
;; → (call foo bar (baz))

call foo:
  bar
  (baz)
  alpha: beta
;; → (call foo bar (baz) alpha: beta)
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_call`

