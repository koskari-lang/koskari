# layout/while

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbols:** `layout/while`, `layout/for-each`

**Use-case:** Block syntax for `while` and `for-each` loops with an optional
`else` peer. Both names share one handler (`layout_loop`); the read result keeps
the block head (`while` or `for-each`) and is compiled by `compile/while` or
`compile/for-each`.

**Behaviour:** Same flat splicing as `layout/flat`: body forms are appended to
the header list (no `compile/begin` wrapper). Accepts at most one optional peer
`else`. When present, the else peer body is appended as a final
`(else: BODY...)` clause (not merged into the main loop body). Rules: only
`else` is allowed as a peer; `else` takes no arguments; at most one `else` peer.

**Syntax:**

```
while TEST:
  BODY
else:
  BODY

for-each [BINDING EXPR]:
  BODY
else:
  BODY
```

**Examples:**

```scheme
while (some_test 123):
  loopbody...
else:
  elsebody...
;; → (while (some_test 123) loopbody... (else: elsebody...))

while x:
  y
;; → (while x y)

for-each [i items]:
  (compile/call append result i)
else:
  "finished"
;; → (for-each [i items] (compile/call append result i) (else: "finished"))
```

**Implementation:** `koskari/reader/layouts/loops.layout_loop` (registered as
`layout/while` and `layout/for-each` via `add_builtin`)

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
