# layout/block (default)
**Symbol:** `layout/block`

**Use-case:** Default layout for blocks whose head has no dedicated
**`layout/<head>`** handler in the namespace. The reader resolves this handler
when **`apply_layout`** or **`apply_layouts`** finds no named layout for the
block head. The original head symbol is preserved; it is not rewritten to
**`layout/block`**.

**Behaviour:** Splices body forms onto the header list:
**`(HEAD ARG ... BODY-FORM ...)`**. Does not wrap the body in **`compile/begin`**.
Does not accept peers.

A body line that opens with a keyword is a call argument name followed by
its value, so it splices as those two elements rather than as an implicit
call. One form after the keyword is the value; several fold to a call:

```koskari
print:
  "hello"
  file: stderr
  sep: 1 2
;; -> (print "hello" file: stderr sep: (1 2))
```

The value may be a block. `k: HEAD ARG ... :` reads the block headed by
**`HEAD`** through that head's own layout, and `k::` reads a **`begin`**
block. A head whose layout gathers peers, such as `if` or `try`, cannot
follow the keyword on the header line, since peers do not pair across a
keyword header; nest that block under `k::` instead:

```koskari
print:
  file: begin:
    (log "choosing")
    stderr
  "hello"
;; -> (print file: (begin (log "choosing") stderr) "hello")

print:
  file::
    if redirect?:
      stdout
    else:
      stderr
  "hello"
;; -> (print file: (begin (cond (redirect? stdout) (else: stderr))) "hello")
```

For a **`compile/begin`** wrapper around the body, use
[layout/block-begin](layout-block-begin.md) instead (for example
**`var layout/my-head layout/block-begin`** in a **`.ki`** module).
