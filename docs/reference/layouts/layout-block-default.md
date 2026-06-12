# layout/block (default)
**Symbol:** `layout/block`

**Use-case:** Default layout for blocks whose head has no dedicated
**`layout/<head>`** handler in the namespace. The reader resolves this handler
when **`apply_layout`** or **`apply_layouts`** finds no named layout for the
block head. The original head symbol is preserved; it is not rewritten to
**`layout/block`**.

**Behaviour:** Splices body forms onto the header list:
**`(HEAD ARG … BODY-FORM …)`**. Does not wrap the body in **`compile/begin`**.
Does not accept peers.

**Syntax:**

```
HEAD ARG ... :
  BODY-FORM
  ...
```

**Examples:**

```koskari
foo x:
  bar
  baz
;; → (foo x bar baz)

unknown x:
  (print x)
;; → (unknown x (print x))

begin:
  (bar)
  (baz)
;; → (begin (bar) (baz))
```

For a **`compile/begin`** wrapper around the body, use
[layout/block-begin](layout-block-begin.md) instead (for example
**`define layout/my-head layout/block-begin`** in a **`.ki`** module).

**Implementation:** `koskari/reader/layouts/basics.layout_block`
