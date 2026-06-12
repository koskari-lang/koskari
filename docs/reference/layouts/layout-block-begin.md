# layout/block-begin
**Symbol:** `layout/block-begin`

**Use-case:** Opt-in layout for block heads that should receive the body wrapped
in **`compile/begin`**. Bind in a **`.ki`** module, for example
**`define layout/my-head layout/block-begin`**, when a head needs sequential
body evaluation inside an explicit begin form.

**Behaviour:** Produces **`(HEAD ARG … (compile/begin BODY …))`**. Does not
accept peers.

**Syntax:**

```
HEAD ARG ... :
  BODY-FORM
  ...
```

**Examples:**

```koskari
;; with define layout/foo layout/block-begin in the module:
foo x:
  bar
  baz
;; → (foo x (compile/begin bar baz))
```

**Implementation:** `koskari/reader/layouts/basics.layout_block_begin`
