# layout/block (default)
**Symbol:** `layout/block`

**Use-case:** The default layout for blocks whose head has no dedicated
**`layout/<head>`** handler in the namespace. The reader still dispatches to the
handler registered as **`layout/block`**, passing the same **`source`** pair
(**header** plus **body** list) that a named layout would receive; the head is
**not** rewritten to **`layout/block`** first.

**Behaviour:** Produces `(HEAD ARGS (compile/begin BODY))`. The body is wrapped
in `compile/begin` for sequential evaluation. Does not accept peers.

**Syntax:**

```
HEAD ARG ... :
  BODY-FORM
  ...
```

**Examples:**

```scheme
;; foo x: ... → (foo x (compile/begin ...))
foo x:
  bar
  baz

;; unknown x: ... → (unknown x (compile/begin ...))
unknown x:
  (print x)
```

**Implementation:** `koskari/reader/layouts/basics.layout_block`

